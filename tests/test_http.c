/* net/http against a local Python server (tests/net_server.py, 127.0.0.1 only), plus the
 * pure file-name helpers of net/dlq_name.c that http_download uses. */
#include "test.h"

#include "net/http.h"

#include <arpa/inet.h>
#include <libgen.h>
#include <limits.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

int dlq_name_disposition(const char *value, char *out, size_t cap);
int dlq_name_from_url(const char *url, char *out, size_t cap);

/* ---- local server (same helper in test_fetch.c / test_dlq.c) ---------------------------- */
static pid_t g_srv;
static int g_port;
static char g_tests[512], g_build[512];

static int port_try(int port, int do_connect)
{
	struct sockaddr_in a;
	int s = socket(AF_INET, SOCK_STREAM, 0), ok;
	memset(&a, 0, sizeof a);
	a.sin_family = AF_INET;
	a.sin_port = htons((uint16_t)port);
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	ok = do_connect ? connect(s, (struct sockaddr *)&a, sizeof a) == 0 : bind(s, (struct sockaddr *)&a, sizeof a) == 0;
	close(s);
	return ok;
}

static int server_start(const char *argv0, int first_port)
{
	char exe[PATH_MAX], script[600], fix[600], ps[16];
	static const char *proxies[] = { "http_proxy", "HTTP_PROXY", "https_proxy", "HTTPS_PROXY", "all_proxy", "ALL_PROXY" };
	for (size_t i = 0; i < sizeof proxies / sizeof proxies[0]; i++) unsetenv(proxies[i]);
	setenv("no_proxy", "*", 1);
	if (!realpath(argv0, exe)) return -1;
	snprintf(g_build, sizeof g_build, "%s", dirname(exe));
	snprintf(exe, sizeof exe, "%s", g_build);
	snprintf(g_tests, sizeof g_tests, "%s", dirname(exe));
	snprintf(script, sizeof script, "%s/net_server.py", g_tests);
	snprintf(fix, sizeof fix, "%s/fixtures", g_tests);
	for (int port = first_port; port < 11700; port++) {
		if (!port_try(port, 0)) continue;
		snprintf(ps, sizeof ps, "%d", port);
		pid_t pid = fork();
		if (pid == 0) {
			execlp("python3", "python3", script, ps, fix, (char *)NULL);
			_exit(127);
		}
		for (int i = 0; i < 200; i++) {
			int st;
			usleep(25000);
			if (port_try(port, 1)) {
				g_srv = pid;
				g_port = port;
				return 0;
			}
			if (waitpid(pid, &st, WNOHANG) == pid) break;
		}
		kill(pid, SIGKILL);
		waitpid(pid, NULL, 0);
	}
	return -1;
}

static void server_stop(void)
{
	if (g_srv > 0) kill(g_srv, SIGTERM), waitpid(g_srv, NULL, 0);
}

static char *U(const char *path)
{
	static char buf[4][256];
	static int k;
	k = (k + 1) % 4;
	snprintf(buf[k], sizeof buf[k], "http://127.0.0.1:%d%s", g_port, path);
	return buf[k];
}

static unsigned char *read_all(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	unsigned char *d = NULL;
	long n;
	*len = 0;
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	d = malloc((size_t)n + 1);
	if (d && fread(d, 1, (size_t)n, f) == (size_t)n) *len = (size_t)n;
	fclose(f);
	return d;
}

static double now_s(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}
/* ---- end of the shared helper ------------------------------------------------------------- */

static unsigned char *fixture(const char *name, size_t *len)
{
	char p[700];
	snprintf(p, sizeof p, "%s/fixtures/%s", g_tests, name);
	return read_all(p, len);
}

/* 1 when the file at path holds exactly want[0..n). */
static int file_is(const char *path, const unsigned char *want, size_t n)
{
	size_t len;
	unsigned char *d = read_all(path, &len);
	int ok = d && want && len == n && memcmp(d, want, n) == 0;
	free(d);
	return ok;
}

static void write_file(const char *path, const void *d, size_t n)
{
	FILE *f = fopen(path, "wb");
	if (f) fwrite(d, 1, n, f), fclose(f);
}

static void get_tags(char *out, size_t cap)
{
	http_doc_t d;
	char err[160];
	out[0] = '\0';
	if (http_get(U("/tags"), NULL, 0, NULL, &d, err, sizeof err) == 0) snprintf(out, cap, "%s", d.data);
	http_doc_free(&d);
}

typedef struct {
	int calls;
	int64_t first, last, total, stop_at;
} prog_t;

static int on_prog(void *ud, int64_t now, int64_t total)
{
	prog_t *p = ud;
	if (p->calls++ == 0) p->first = now;
	p->last = now;
	p->total = total;
	return p->stop_at > 0 && now >= p->stop_at;
}

static volatile int g_cancel;
static void *cancel_later(void *arg)
{
	(void)arg;
	usleep(300000);
	g_cancel = 1;
	return NULL;
}

static void test_names(void)
{
	char n[256];
	CHECK(dlq_name_disposition("attachment; filename=\"a b.epub\"", n, sizeof n));
	CHECK_STR(n, "a b.epub");
	CHECK(dlq_name_disposition("attachment; filename=plain.cbz ; size=3", n, sizeof n));
	CHECK_STR(n, "plain.cbz");
	CHECK(dlq_name_disposition("attachment; filename*=UTF-8''%E6%97%A5%E6%9C%AC.epub; filename=\"x.epub\"", n, sizeof n));
	CHECK_STR(n, "\xe6\x97\xa5\xe6\x9c\xac.epub");
	CHECK(dlq_name_disposition("inline; filename=\"x.epub\"; FileName*=utf-8'en'caf%C3%A9.epub", n, sizeof n));
	CHECK_STR(n, "caf\xc3\xa9.epub");
	CHECK(dlq_name_disposition("attachment; filename*=iso-8859-1''caf%E9.epub; filename=\"cafe.epub\"", n, sizeof n));
	CHECK_STR(n, "cafe.epub");
	CHECK(dlq_name_disposition("attachment; filename=\"q\\\"uote;d.epub\"", n, sizeof n));
	CHECK_STR(n, "q_uote;d.epub");
	CHECK(dlq_name_disposition("attachment; filename=\"../../etc/passwd\"", n, sizeof n));
	CHECK_STR(n, "passwd");
	CHECK(dlq_name_disposition("attachment; filename=\"C:\\\\dir\\\\a<b>:c|d?.epub\"", n, sizeof n));
	CHECK_STR(n, "a_b__c_d_.epub");
	CHECK(dlq_name_disposition("attachment; filename*=UTF-8''..%2F.hidden.epub. ", n, sizeof n));
	CHECK_STR(n, "hidden.epub");
	CHECK(!dlq_name_disposition("attachment", n, sizeof n));
	CHECK(!dlq_name_disposition("attachment; filename=\"..\"", n, sizeof n));
	CHECK(!dlq_name_disposition("", n, sizeof n));
	CHECK(!dlq_name_disposition(NULL, n, sizeof n));
	CHECK(dlq_name_disposition("attachment; filename=\"\xe6\x97\xa5\xe6\x9c\xac.epub\"", n, 5));
	CHECK_STR(n, "\xe6\x97\xa5"); /* cut on a character boundary */
	CHECK(dlq_name_from_url("http://h/a/b%20c.epub?x=1#f", n, sizeof n));
	CHECK_STR(n, "b c.epub");
	CHECK(dlq_name_from_url("https://h:8080/get/%E6%97%A5.cbz", n, sizeof n));
	CHECK_STR(n, "\xe6\x97\xa5.cbz");
	CHECK(dlq_name_from_url("http://h/a%00b%2Fc", n, sizeof n));
	CHECK_STR(n, "a_b_c");
	CHECK(!dlq_name_from_url("http://h/", n, sizeof n));
	CHECK(!dlq_name_from_url("http://h", n, sizeof n));
	CHECK(!dlq_name_from_url("http://h/dir/?q=a/b.epub", n, sizeof n));
}

static void test_get(void)
{
	http_doc_t d;
	char err[160];
	size_t flen;
	unsigned char *txt = fixture("minimal.txt", &flen);
	http_auth_t good = { "user", "pass" }, bad = { "user", "nope" };

	CHECK_INT(http_get(U("/f/minimal.txt"), NULL, 1 << 20, NULL, &d, err, sizeof err), 0);
	CHECK_STR(err, "");
	CHECK_INT(d.status, 200);
	CHECK(txt && d.len == flen && memcmp(d.data, txt, flen) == 0 && d.data[d.len] == '\0');
	CHECK_STR(d.content_type, "text/plain");
	CHECK_STR(d.final_url, U("/f/minimal.txt"));
	http_doc_free(&d);
	CHECK(d.data == NULL && d.len == 0);

	CHECK_INT(http_get(U("/redirect/minimal.txt"), NULL, 1 << 20, NULL, &d, err, sizeof err), 0);
	CHECK_STR(d.final_url, U("/f/minimal.txt"));
	CHECK(d.len == flen);
	http_doc_free(&d);

	CHECK_INT(http_get(U("/404"), NULL, 1 << 20, NULL, &d, err, sizeof err), -1);
	CHECK_STR(err, "HTTP 404");
	CHECK_INT(d.status, 404);
	CHECK(d.data == NULL && d.len == 0 && d.final_url[0] == '\0');

	CHECK_INT(http_get(U("/big?n=5000"), NULL, 1000, NULL, &d, err, sizeof err), -1);
	CHECK_STR(err, "Too large");
	CHECK(d.data == NULL);
	CHECK_INT(http_get(U("/bigchunked?n=5000"), NULL, 1000, NULL, &d, err, sizeof err), -1);
	CHECK_STR(err, "Too large");
	CHECK_INT(http_get(U("/bigchunked?n=5000"), NULL, 5000, NULL, &d, err, sizeof err), 0);
	CHECK_INT(d.len, 5000);
	http_doc_free(&d);
	CHECK_INT(http_get(U("/big?n=5000"), NULL, 5000, NULL, &d, err, sizeof err), 0);
	CHECK_INT(d.len, 5000);
	http_doc_free(&d);

	CHECK_INT(http_get(U("/auth/minimal.txt"), &good, 1 << 20, NULL, &d, err, sizeof err), 0);
	CHECK(d.len == flen);
	http_doc_free(&d);
	CHECK_INT(http_get(U("/auth/minimal.txt"), NULL, 1 << 20, NULL, &d, err, sizeof err), -1);
	CHECK_STR(err, "HTTP 401");
	CHECK_INT(http_get(U("/auth/minimal.txt"), &bad, 1 << 20, NULL, &d, err, sizeof err), -1);
	CHECK_STR(err, "HTTP 401");

	pthread_t t;
	g_cancel = 0;
	pthread_create(&t, NULL, cancel_later, NULL);
	double t0 = now_s();
	CHECK_INT(http_get(U("/slow?n=100&ms=100"), NULL, 1 << 20, &g_cancel, &d, err, sizeof err), -1);
	double el = now_s() - t0;
	pthread_join(t, NULL);
	CHECK_STR(err, "Cancelled");
	CHECK(el < 3.0);
	CHECK(d.data == NULL);

	CHECK_INT(http_get("http://127.0.0.1:1/x", NULL, 0, NULL, &d, err, sizeof err), -1);
	CHECK_STR(err, "Could not connect");
	CHECK_INT(d.status, 0);
	CHECK_INT(http_get("ftp://127.0.0.1/x", NULL, 0, NULL, &d, err, sizeof err), -1);
	CHECK_STR(err, "Bad URL");
	free(txt);
}

static void test_download(void)
{
	char part[700], err[160], name[256];
	size_t jlen;
	unsigned char *jpg = fixture("tall_800x9000.jpg", &jlen);
	prog_t p;
	snprintf(part, sizeof part, "%s/http_dl.part", g_build);
	remove(part);

	memset(&p, 0, sizeof p);
	CHECK_INT(http_download(U("/f/tall_800x9000.jpg"), NULL, part, 0, on_prog, &p, name, sizeof name, err, sizeof err), 0);
	CHECK(file_is(part, jpg, jlen));
	CHECK_STR(name, "tall_800x9000.jpg");
	CHECK(p.calls > 0 && p.last == (int64_t)jlen && p.total == (int64_t)jlen);

	char tags[256];
	get_tags(tags, sizeof tags);
	write_file(part, jpg, 50000); /* resume: the first 50000 bytes are already there */
	memset(&p, 0, sizeof p);
	CHECK_INT(http_download(U("/f/tall_800x9000.jpg"), NULL, part, 50000, on_prog, &p, NULL, 0, err, sizeof err), 0);
	CHECK(file_is(part, jpg, jlen));
	get_tags(tags, sizeof tags);
	CHECK_STR(tags, "range:50000");
	CHECK(p.first >= 50000 && p.last == (int64_t)jlen && p.total == (int64_t)jlen);

	unsigned char junk[50000]; /* server ignores Range: the 200 body must replace the .part */
	memset(junk, 'Z', sizeof junk);
	write_file(part, junk, sizeof junk);
	CHECK_INT(http_download(U("/norange/tall_800x9000.jpg"), NULL, part, 50000, NULL, NULL, NULL, 0, err, sizeof err), 0);
	CHECK(file_is(part, jpg, jlen));

	write_file(part, junk, 195); /* the same when the .part is as long as the file */
	CHECK_INT(http_download(U("/norange/minimal.txt"), NULL, part, 195, NULL, NULL, NULL, 0, err, sizeof err), 0);
	size_t tlen;
	unsigned char *txt = fixture("minimal.txt", &tlen);
	CHECK(tlen == 195 && file_is(part, txt, tlen));
	free(txt);

	write_file(part, junk, 10); /* .part shorter than resume_from: start over */
	CHECK_INT(http_download(U("/f/tall_800x9000.jpg"), NULL, part, 50000, NULL, NULL, NULL, 0, err, sizeof err), 0);
	CHECK(file_is(part, jpg, jlen));
	get_tags(tags, sizeof tags);
	CHECK_STR(tags, "");

	write_file(part, jpg, jlen); /* range past the end: 416, restarted from 0 */
	CHECK_INT(http_download(U("/f/tall_800x9000.jpg"), NULL, part, (int64_t)jlen, NULL, NULL, NULL, 0, err, sizeof err), 0);
	CHECK(file_is(part, jpg, jlen));
	get_tags(tags, sizeof tags);
	CHECK_STR(tags, "range:211937");

	CHECK_INT(http_download(U("/cd"), NULL, part, 0, NULL, NULL, name, sizeof name, err, sizeof err), 0);
	CHECK_STR(name, "\xe6\x97\xa5\xe6\x9c\xac.epub");
	CHECK_INT(http_download(U("/redirect/minimal.epub"), NULL, part, 0, NULL, NULL, name, sizeof name, err, sizeof err), 0);
	CHECK_STR(name, "minimal.epub");

	http_auth_t good = { "user", "pass" };
	CHECK_INT(http_download(U("/auth/minimal.txt"), &good, part, 0, NULL, NULL, NULL, 0, err, sizeof err), 0);
	CHECK_INT(http_download(U("/auth/minimal.txt"), NULL, part, 0, NULL, NULL, NULL, 0, err, sizeof err), -1);
	CHECK_STR(err, "HTTP 401");

	remove(part);
	CHECK_INT(http_download(U("/404"), NULL, part, 0, NULL, NULL, name, sizeof name, err, sizeof err), -1);
	CHECK_STR(err, "HTTP 404");
	CHECK(access(part, F_OK) != 0); /* an error reply never creates the file */
	CHECK_STR(name, "");

	write_file(part, jpg, 1000);
	CHECK_INT(http_download(U("/flaky/minimal.txt"), NULL, part, 1000, NULL, NULL, NULL, 0, err, sizeof err), -1);
	CHECK_STR(err, "HTTP 503");
	CHECK(file_is(part, jpg, 1000)); /* a failed reply leaves the .part alone */
	/* a 206 from the wrong offset is never appended: the download starts over */
	CHECK_INT(http_download(U("/badrange/tall_800x9000.jpg"), NULL, part, 1000, NULL, NULL, NULL, 0, err,
	                        sizeof err), 0);
	CHECK(file_is(part, jpg, jlen));

	memset(&p, 0, sizeof p);
	p.stop_at = 10;
	double t0 = now_s();
	CHECK_INT(http_download(U("/slow?n=100&ms=50"), NULL, part, 0, on_prog, &p, NULL, 0, err, sizeof err), -1);
	CHECK_STR(err, "Cancelled");
	CHECK(now_s() - t0 < 4.0);
	remove(part);
	free(jpg);
}

int main(int argc, char **argv)
{
	http_doc_t d;
	char err[160];
	(void)argc;
	CHECK_INT(http_get("http://127.0.0.1:1/", NULL, 0, NULL, &d, err, sizeof err), -1);
	CHECK_STR(err, "Network unavailable");
	test_names();
	CHECK_INT(http_global_init(NULL), 0);
	CHECK_INT(server_start(argv[0], 11610), 0);
	if (g_port) {
		test_get();
		test_download();
	}
	server_stop();
	http_global_cleanup();
	return TEST_DONE("http");
}
