#include "net/http.h"

#include "core/strutil.h"

#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define CONNECT_TIMEOUT_S 20L
#define STALL_TIMEOUT_S 60L /* below 1 byte/s for this long = stalled */
#define MAX_REDIRS 10L
#define INITIAL_CAP 8192

/* Vita network bring-up lives in http_vita.c; the host has nothing to bring up. */
#ifdef __vita__
int http_vita_net_up(void);
void http_vita_net_down(void);
#else
static int http_vita_net_up(void) { return 0; }
static void http_vita_net_down(void) {}
#endif

/* File-name helpers in net/dlq_name.c (shared with the download queue, no header). */
int dlq_name_disposition(const char *value, char *out, size_t cap);
int dlq_name_from_url(const char *url, char *out, size_t cap);

static char g_ca[512];
static int g_ready, g_easy_key_ready;
static pthread_key_t g_easy_key;

static void easy_destroy(void *c) { curl_easy_cleanup(c); }
static CURL *easy_acquire(void)
{
	CURL *c = g_easy_key_ready ? pthread_getspecific(g_easy_key) : NULL;
	if (!c) {
		c = curl_easy_init();
		if (c && g_easy_key_ready && pthread_setspecific(g_easy_key, c)) {
			curl_easy_cleanup(c);
			return NULL;
		}
	}
	if (c) curl_easy_reset(c); /* retains connections, DNS and TLS sessions */
	return c;
}

static void easy_release(CURL *c)
{
	/* Clear stack-based callbacks and credentials, retaining only curl's caches. */
	if (g_easy_key_ready) curl_easy_reset(c);
	else curl_easy_cleanup(c);
}

typedef struct {
	CURL *c;
	const volatile int *cancel;
	http_progress_fn progress;
	void *ud;
	int cancelled;
	/* memory sink (http_get) */
	char *data;
	size_t len, cap, max;
	int too_large, oom;
	/* file sink (http_download) */
	const char *path;
	FILE *f;
	int64_t offset; /* bytes already in the file before this reply's body */
	int write_err, bad_range;
	char *file_buffer;
	/* headers of the latest reply (each redirect hop starts over) */
	int64_t range_start; /* Content-Range start, -1 when absent */
	char disp[512];
} xfer_t;

int http_global_init(const char *ca_file)
{
	if (g_ready) return 0;
	if (http_vita_net_up() != 0) return -1;
	if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK) {
		http_vita_net_down();
		return -1;
	}
	g_easy_key_ready = pthread_key_create(&g_easy_key, easy_destroy) == 0;
	str_copy(g_ca, sizeof g_ca, ca_file ? ca_file : "");
	g_ready = 1;
	return 0;
}

void http_global_cleanup(void)
{
	if (!g_ready) return;
	/* All HTTP worker threads must be joined before global cleanup. */
	if (g_easy_key_ready) {
		CURL *c = pthread_getspecific(g_easy_key);
		if (c) { pthread_setspecific(g_easy_key, NULL); curl_easy_cleanup(c); }
		pthread_key_delete(g_easy_key);
		g_easy_key_ready = 0;
	}
	curl_global_cleanup();
	http_vita_net_down();
	g_ready = 0;
}

static void set_err(char *err, size_t cap, const char *msg)
{
	if (err && cap) str_copy(err, cap, msg);
}

static long reply_status(xfer_t *x)
{
	long status = 0;
	curl_easy_getinfo(x->c, CURLINFO_RESPONSE_CODE, &status);
	return status;
}

static int xferinfo_cb(void *ud, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
	xfer_t *x = (xfer_t *)ud;
	(void)ultotal;
	(void)ulnow;
	if (x->cancel && *x->cancel) x->cancelled = 1;
	else if (x->progress && x->progress(x->ud, x->offset + dlnow, dltotal > 0 ? x->offset + dltotal : -1))
		x->cancelled = 1;
	return x->cancelled;
}

/* Value of header line h (length n) when its name is name, trimmed into out. */
static int header_value(const char *h, size_t n, const char *name, char *out, size_t cap)
{
	size_t k = strlen(name), i = 0;
	if (n <= k || h[k] != ':') return 0;
	for (; i < k; i++) {
		char c = h[i];
		if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
		if (c != name[i]) return 0;
	}
	h += k + 1;
	n -= k + 1;
	while (n > 0 && (*h == ' ' || *h == '\t')) h++, n--;
	while (n > 0 && (h[n - 1] == '\r' || h[n - 1] == '\n' || h[n - 1] == ' ')) n--;
	if (n >= cap) n = cap - 1;
	memcpy(out, h, n);
	out[n] = '\0';
	return 1;
}

static size_t header_cb(char *data, size_t size, size_t nmemb, void *ud)
{
	xfer_t *x = (xfer_t *)ud;
	size_t n = size * nmemb;
	char v[64];
	if (n >= 5 && memcmp(data, "HTTP/", 5) == 0) {
		x->disp[0] = '\0';
		x->range_start = -1;
	} else if (header_value(data, n, "content-disposition", x->disp, sizeof x->disp)) {
	} else if (header_value(data, n, "content-range", v, sizeof v) && strncmp(v, "bytes ", 6) == 0) {
		x->range_start = strtoll(v + 6, NULL, 10);
	}
	return n;
}

static size_t mem_write(char *p, size_t size, size_t nmemb, void *ud)
{
	xfer_t *x = (xfer_t *)ud;
	size_t n = size * nmemb;
	long st = reply_status(x);
	if (st < 200 || st > 299) return n; /* body of an error or redirect reply: dropped */
	if (x->max && (n > x->max || x->len > x->max - n)) {
		x->too_large = 1;
		return 0;
	}
	if (x->len + n + 1 > x->cap) {
		size_t cap = x->cap ? x->cap : INITIAL_CAP;
		while (cap < x->len + n + 1) cap *= 2;
		char *d = realloc(x->data, cap);
		if (!d) {
			x->oom = 1;
			return 0;
		}
		x->data = d;
		x->cap = cap;
	}
	memcpy(x->data + x->len, p, n);
	x->len += n;
	x->data[x->len] = '\0';
	return n;
}

/* Opened on the first body byte of the final reply: a 206 continues the file, anything else
 * (a 200 to a Range request included) starts it over. */
static int open_sink(xfer_t *x)
{
	if (x->offset > 0 && reply_status(x) == 206) {
		if (x->range_start != x->offset) {
			x->bad_range = 1;
			return -1;
		}
		x->f = fopen(x->path, "ab");
	} else {
		x->offset = 0;
		x->f = fopen(x->path, "wb");
	}
	if (!x->f) x->write_err = 1;
	else {
		x->file_buffer = malloc(128u * 1024u);
		if (x->file_buffer && setvbuf(x->f, x->file_buffer, _IOFBF, 128u * 1024u)) {
			free(x->file_buffer); x->file_buffer = NULL;
		}
	}
	return x->f ? 0 : -1;
}

static size_t file_write(char *p, size_t size, size_t nmemb, void *ud)
{
	xfer_t *x = (xfer_t *)ud;
	size_t n = size * nmemb;
	long st = reply_status(x);
	if (st < 200 || st > 299) return n;
	if (!x->f && open_sink(x) != 0) return 0;
	if (fwrite(p, 1, n, x->f) != n) {
		x->write_err = 1;
		return 0;
	}
	return n;
}

static CURL *easy_new(const char *url, const http_auth_t *auth, xfer_t *x, char *errbuf)
{
	CURL *c = easy_acquire();
	if (!c) return NULL;
	x->c = c;
	x->range_start = -1;
	errbuf[0] = '\0';
	curl_easy_setopt(c, CURLOPT_URL, url);
	curl_easy_setopt(c, CURLOPT_USERAGENT, HTTP_USER_AGENT);
	curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "http,https");
	/* Follow redirects, but never let an https request be walked down to plain http. */
	curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, strncasecmp(url, "https:", 6) == 0 ? "https" : "http,https");
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(c, CURLOPT_MAXREDIRS, MAX_REDIRS);
	curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, CONNECT_TIMEOUT_S);
	curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L);
	curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, STALL_TIMEOUT_S);
	curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(c, CURLOPT_MAXCONNECTS, 4L);
	curl_easy_setopt(c, CURLOPT_BUFFERSIZE, 64L * 1024L);
	curl_easy_setopt(c, CURLOPT_TCP_NODELAY, 1L);
	if (g_ca[0]) curl_easy_setopt(c, CURLOPT_CAINFO, g_ca);
	curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
	curl_easy_setopt(c, CURLOPT_ERRORBUFFER, errbuf);
	curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, xferinfo_cb);
	curl_easy_setopt(c, CURLOPT_XFERINFODATA, x);
	curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, header_cb);
	curl_easy_setopt(c, CURLOPT_HEADERDATA, x);
	if (auth && auth->user && auth->user[0]) {
		curl_easy_setopt(c, CURLOPT_HTTPAUTH, (long)CURLAUTH_BASIC);
		curl_easy_setopt(c, CURLOPT_USERNAME, auth->user);
		curl_easy_setopt(c, CURLOPT_PASSWORD, auth->pass ? auth->pass : "");
	}
	return c;
}

/* 0 when the transfer succeeded, else -1 with a short reason in err. */
static int result(xfer_t *x, CURLcode rc, long status, const char *errbuf, char *err, size_t cap)
{
	char msg[160];
	if (x->cancelled) set_err(err, cap, "Cancelled");
	else if (x->too_large || rc == CURLE_FILESIZE_EXCEEDED) set_err(err, cap, "Too large");
	else if (x->oom || rc == CURLE_OUT_OF_MEMORY) set_err(err, cap, "Out of memory");
	else if (x->write_err) set_err(err, cap, "Could not write the file");
	else if (x->bad_range) set_err(err, cap, "Bad Content-Range in reply");
	else if (rc == CURLE_OK || rc == CURLE_HTTP_RETURNED_ERROR) {
		if (status >= 200 && status <= 299) return 0;
		snprintf(msg, sizeof msg, "HTTP %ld", status);
		set_err(err, cap, msg);
	} else if (rc == CURLE_COULDNT_RESOLVE_HOST) set_err(err, cap, "Could not resolve host");
	else if (rc == CURLE_COULDNT_CONNECT) set_err(err, cap, "Could not connect");
	else if (rc == CURLE_OPERATION_TIMEDOUT) set_err(err, cap, "Timed out");
	else if (rc == CURLE_TOO_MANY_REDIRECTS) set_err(err, cap, "Too many redirects");
	else if (rc == CURLE_URL_MALFORMAT || rc == CURLE_UNSUPPORTED_PROTOCOL) set_err(err, cap, "Bad URL");
	else if (rc == CURLE_PEER_FAILED_VERIFICATION || rc == CURLE_SSL_CONNECT_ERROR ||
	         rc == CURLE_SSL_CACERT_BADFILE)
		set_err(err, cap, "Secure connection failed");
	else set_err(err, cap, errbuf[0] ? errbuf : curl_easy_strerror(rc));
	return -1;
}

int http_get(const char *url, const http_auth_t *auth, size_t max_bytes, const volatile int *cancel,
			 http_doc_t *out, char *err, size_t errcap)
{
	char errbuf[CURL_ERROR_SIZE];
	xfer_t x;
	memset(out, 0, sizeof *out);
	memset(&x, 0, sizeof x);
	set_err(err, errcap, "");
	if (!url || !*url) {
		set_err(err, errcap, "Bad URL");
		return -1;
	}
	if (!g_ready) {
		set_err(err, errcap, "Network unavailable");
		return -1;
	}
	x.cancel = cancel;
	x.max = max_bytes;
	CURL *c = easy_new(url, auth, &x, errbuf);
	if (!c) {
		set_err(err, errcap, "Out of memory");
		return -1;
	}
	if (max_bytes) curl_easy_setopt(c, CURLOPT_MAXFILESIZE_LARGE, (curl_off_t)max_bytes);
	/* Vita curl's negotiated compression can fail inside its decoder. Request
	 * plain metadata/catalogue responses; keep decoding enabled for servers
	 * that ignore identity, and retain the memory limit in mem_write. */
	curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "identity");
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, mem_write);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, &x);
	CURLcode rc = curl_easy_perform(c);
	long status = reply_status(&x);
	if (rc == CURLE_OK && !x.data && !(x.data = calloc(1, 1))) x.oom = 1; /* empty body */
	if (result(&x, rc, status, errbuf, err, errcap) == 0) {
		const char *s = NULL;
		out->data = x.data;
		out->len = x.len;
		out->status = status;
		if (curl_easy_getinfo(c, CURLINFO_CONTENT_TYPE, &s) == CURLE_OK && s)
			str_copy(out->content_type, sizeof out->content_type, s);
		s = NULL;
		curl_easy_getinfo(c, CURLINFO_EFFECTIVE_URL, &s);
		str_copy(out->final_url, sizeof out->final_url, s ? s : url);
		easy_release(c);
		return 0;
	}
	easy_release(c);
	free(x.data);
	out->status = status;
	return -1;
}

void http_doc_free(http_doc_t *d)
{
	if (!d) return;
	free(d->data);
	memset(d, 0, sizeof *d);
}

int http_download(const char *url, const http_auth_t *auth, const char *part_path,
				  int64_t resume_from, http_progress_fn progress, void *ud, char *filename,
				  size_t filename_cap, char *err, size_t errcap)
{
	char errbuf[CURL_ERROR_SIZE];
	struct stat st;
	xfer_t x;
	memset(&x, 0, sizeof x);
	if (filename && filename_cap) filename[0] = '\0';
	set_err(err, errcap, "");
	if (!url || !*url || !part_path || !*part_path) {
		set_err(err, errcap, "Bad URL");
		return -1;
	}
	if (!g_ready) {
		set_err(err, errcap, "Network unavailable");
		return -1;
	}
	x.progress = progress;
	x.ud = ud;
	x.path = part_path;
	if (resume_from > 0 && stat(part_path, &st) == 0 && (int64_t)st.st_size == resume_from)
		x.offset = resume_from;
	CURL *c = easy_new(url, auth, &x, errbuf);
	if (!c) {
		set_err(err, errcap, "Out of memory");
		return -1;
	}
	if (x.offset > 0) curl_easy_setopt(c, CURLOPT_RESUME_FROM_LARGE, (curl_off_t)x.offset);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, file_write);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, &x);
	CURLcode rc = curl_easy_perform(c);
	long status = reply_status(&x);
	if (x.offset > 0 && !x.f && !x.cancelled &&
	    (rc == CURLE_RANGE_ERROR || status == 416 || (rc == CURLE_OK && status == 200))) {
		/* The server would not continue the .part: no Range support (curl refuses the 200, or
		 * skips its body when the size equals the offset), or the range starts at or past the
		 * end. The .part is not a prefix we can trust: start over. */
		easy_release(c);
		return http_download(url, auth, part_path, 0, progress, ud, filename, filename_cap, err, errcap);
	}
	if (!x.f && rc == CURLE_OK && status >= 200 && status <= 299 && !x.cancelled)
		open_sink(&x); /* empty body: still an (empty) file */
	if (x.f && fclose(x.f) != 0) x.write_err = 1;
	free(x.file_buffer);
	int r = result(&x, rc, status, errbuf, err, errcap);
	if (r == 0 && filename && filename_cap) {
		const char *eff = NULL;
		curl_easy_getinfo(c, CURLINFO_EFFECTIVE_URL, &eff);
		if (!dlq_name_disposition(x.disp, filename, filename_cap))
			dlq_name_from_url(eff ? eff : url, filename, filename_cap);
	}
	easy_release(c);
	return r;
}
