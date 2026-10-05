/* net/dlq end to end against a local Python server (tests/net_server.py, 127.0.0.1 only) with a
 * temp sqlite db: naming, .part + rename, cancel, retry, remove, stop and resume on restart. */
#include "test.h"

#include "lib/db.h"
#include "net/dlq.h"
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

void dlq_name_ext(const char *ext, const char *url, char *out, size_t cap);
int dlq_name_make(const char *dir, const char *title, const char *ext, const char *url,
                  int (*taken)(const char *path, void *ud), void *ud, char *out, size_t cap);

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

static int never_taken(const char *path, void *ud)
{
	(void)path;
	(void)ud;
	return 0;
}

/* "Same.cbz" and "Same (2).cbz" are taken. */
static int taken_twice(const char *path, void *ud)
{
	(void)ud;
	return strstr(path, "(3)") == NULL;
}

static char g_books[600];

static int file_is(const char *path, const unsigned char *want, size_t n)
{
	size_t len;
	unsigned char *d = read_all(path, &len);
	int ok = d && want && len == n && memcmp(d, want, n) == 0;
	free(d);
	return ok;
}

static unsigned char *fixture(const char *name, size_t *len)
{
	char p[700];
	snprintf(p, sizeof p, "%s/fixtures/%s", g_tests, name);
	return read_all(p, len);
}

static const char *book(const char *name)
{
	static char buf[2][800];
	static int k;
	k ^= 1;
	snprintf(buf[k], sizeof buf[k], "%s/%s", g_books, name);
	return buf[k];
}

static int exists(const char *path)
{
	return access(path, F_OK) == 0;
}

static int item(int64_t id, dlq_item_t *out)
{
	dlq_item_t all[DLQ_MAX];
	int n = dlq_list(all, DLQ_MAX);
	for (int i = 0; i < n; i++)
		if (all[i].id == id) return *out = all[i], 1;
	memset(out, 0, sizeof *out);
	return 0;
}

static int db_row(int64_t id, db_download_t *out)
{
	static db_download_t rows[DLQ_MAX * 2];
	int n = db_download_list(rows, DLQ_MAX * 2);
	for (int i = 0; i < n; i++)
		if (rows[i].id == id) return *out = rows[i], 1;
	return 0;
}

/* Pump like the main loop until nothing is queued or running (15 s at most). */
static int pump_idle(void)
{
	int done = 0;
	for (int i = 0; i < 1500 && dlq_busy(); i++) {
		done += dlq_pump();
		usleep(10000);
	}
	return done + dlq_pump();
}

/* Pump until download id is in state (and, for ACTIVE, has bytes). */
static int pump_until(int64_t id, int state)
{
	dlq_item_t it;
	for (int i = 0; i < 1500; i++) {
		dlq_pump();
		if (item(id, &it) && it.state == state && (state != DB_DL_ACTIVE || it.bytes > 0)) return 1;
		usleep(10000);
	}
	return 0;
}

static void test_names(void)
{
	char e[16], p[512];
	dlq_name_ext(".EPUB", NULL, e, sizeof e);
	CHECK_STR(e, "epub");
	dlq_name_ext(NULL, "http://h/x/Book.CBZ?y=1", e, sizeof e);
	CHECK_STR(e, "cbz");
	dlq_name_ext("e p", "http://h/a.pdf", e, sizeof e);
	CHECK_STR(e, "pdf");
	dlq_name_ext("", "http://h/get?id=3", e, sizeof e);
	CHECK_STR(e, "bin");
	CHECK_INT(dlq_name_make("ux0:books", "A: B/C?", "epub", NULL, never_taken, NULL, p, sizeof p), 0);
	CHECK_STR(p, "ux0:books/A_ B_C_.epub");
	CHECK_INT(dlq_name_make("ux0:books", " ..", "epub", NULL, never_taken, NULL, p, sizeof p), 0);
	CHECK_STR(p, "ux0:books/Download.epub");
	CHECK_INT(dlq_name_make("ux0:books", "Same", "cbz", NULL, taken_twice, NULL, p, sizeof p), 0);
	CHECK_STR(p, "ux0:books/Same (3).cbz");
	char longt[400];
	memset(longt, 'a', sizeof longt);
	memcpy(longt + 118, "\xe6\x97\xa5", 3); /* a 3-byte character across the 120-byte cut */
	longt[sizeof longt - 1] = '\0';
	CHECK_INT(dlq_name_make("d", longt, "epub", NULL, never_taken, NULL, p, sizeof p), 0);
	CHECK_INT((long long)strlen(p), 2 + 118 + 5);
	CHECK_INT(dlq_name_make("d", "x", "epub", NULL, never_taken, NULL, p, 6), -1); /* does not fit */
}

static void test_queue(void)
{
	size_t elen, tlen;
	unsigned char *epub = fixture("minimal.epub", &elen), *txt = fixture("minimal.txt", &tlen);
	int64_t a, b, c, s, f, x, y;
	dlq_item_t it;
	db_download_t row;
	http_auth_t good = { "user", "pass" };

	FILE *fp = fopen(book("Book.epub"), "wb"); /* already in the library: never overwritten */
	if (fp) fputs("old", fp), fclose(fp);
	CHECK_INT(dlq_add(U("/f/minimal.epub"), NULL, "Book", "epub", &a), 0);
	CHECK_INT(dlq_add(U("/f/minimal.epub"), NULL, "Book", "epub", &b), 0);
	CHECK_INT(dlq_add(U("/redirect/minimal.epub"), NULL, "Minimal: A/B?", NULL, &c), 0);
	CHECK_INT(dlq_add(U("/auth/minimal.txt"), &good, "Secret", "txt", &s), 0);
	CHECK_INT(dlq_add(U("/auth/minimal.txt"), NULL, "Locked", "txt", &x), 0);
	CHECK_INT(dlq_add(U("/flaky/minimal.txt"), NULL, "Flaky", "txt", &f), 0);
	CHECK(dlq_busy());
	CHECK_INT(pump_idle(), 4);
	CHECK(!dlq_busy());
	CHECK(item(a, &it) && it.state == DB_DL_DONE);
	CHECK_STR(it.dest_path, book("Book (2).epub"));
	CHECK(it.bytes == (int64_t)elen && it.total == (int64_t)elen);
	CHECK(file_is(book("Book (2).epub"), epub, elen) && file_is(book("Book (3).epub"), epub, elen));
	CHECK(file_is(book("Minimal_ A_B_.epub"), epub, elen));
	CHECK(file_is(book("Secret.txt"), txt, tlen));
	CHECK(file_is(book("Book.epub"), (const unsigned char *)"old", 3));
	CHECK(!exists(book("Book (2).epub.part")) && !exists(book("Minimal_ A_B_.epub.part")));
	CHECK(db_row(a, &row) && row.state == DB_DL_DONE && row.bytes == (int64_t)elen);
	CHECK_STR(row.dest_path, book("Book (2).epub"));
	CHECK(db_row(x, &row) && row.state == DB_DL_FAILED);
	CHECK_STR(row.error, "HTTP 401");
	CHECK(item(f, &it) && it.state == DB_DL_FAILED);
	CHECK_STR(it.error, "HTTP 503");

	dlq_item_t all[DLQ_MAX];
	CHECK_INT(dlq_list(all, DLQ_MAX), 6);
	CHECK(all[0].id == a && all[1].id == b && all[5].id == f);
	CHECK_INT(dlq_list(all, 2), 2);
	CHECK(all[0].id == a && all[1].id == b);

	CHECK_INT(dlq_retry(a), -1); /* only FAILED / CANCELLED */
	CHECK_INT(dlq_retry(f), 0);
	CHECK_INT(pump_idle(), 1);
	CHECK(item(f, &it) && it.state == DB_DL_DONE && it.error[0] == '\0');
	CHECK(db_row(f, &row) && row.state == DB_DL_DONE && row.error[0] == '\0');
	CHECK(file_is(book("Flaky.txt"), txt, tlen));

	CHECK_INT(dlq_remove(a), 0); /* the book stays */
	CHECK(!item(a, &it) && !db_row(a, &row));
	CHECK(exists(book("Book (2).epub")));
	CHECK_INT(dlq_remove(a), -1);

	/* cancel: running and queued */
	CHECK_INT(dlq_add(U("/slow?n=200&ms=50"), NULL, "Slow", "bin", &y), 0);
	int64_t q;
	CHECK_INT(dlq_add(U("/f/minimal.epub"), NULL, "Queued", "epub", &q), 0);
	CHECK(pump_until(y, DB_DL_ACTIVE));
	CHECK(exists(book("Slow.bin.part")));
	CHECK_INT(dlq_remove(y), -1); /* active */
	CHECK_INT(dlq_cancel(q), 0);
	CHECK(item(q, &it) && it.state == DB_DL_CANCELLED);
	double t0 = now_s();
	CHECK_INT(dlq_cancel(y), 0);
	CHECK(pump_until(y, DB_DL_CANCELLED));
	CHECK(now_s() - t0 < 3.0);
	CHECK(!exists(book("Slow.bin.part")) && !exists(book("Slow.bin")) && !exists(book("Queued.epub")));
	dlq_pump();
	CHECK(db_row(y, &row) && row.state == DB_DL_CANCELLED);
	CHECK(db_row(q, &row) && row.state == DB_DL_CANCELLED);
	CHECK_INT(dlq_cancel(y), -1);
	CHECK_INT(dlq_cancel(b), -1); /* done */
	CHECK_INT(dlq_remove(y), 0);
	CHECK(!db_row(y, &row));
	free(epub);
	free(txt);
}

static void get_tags(char *out, size_t cap)
{
	http_doc_t d;
	char err[160];
	out[0] = '\0';
	if (http_get(U("/tags"), NULL, 0, NULL, &d, err, sizeof err) == 0) snprintf(out, cap, "%s", d.data);
	http_doc_free(&d);
}

static void test_restart(void)
{
	size_t jlen;
	unsigned char *jpg = fixture("tall_800x9000.jpg", &jlen);
	int64_t st, rs, r2;
	dlq_item_t it;
	db_download_t row;
	char tags[256];

	CHECK_INT(dlq_add(U("/slow?n=60&ms=50"), NULL, "Stopme", "bin", &st), 0);
	CHECK(pump_until(st, DB_DL_ACTIVE));
	dlq_stop(); /* the .part stays, the row stays ACTIVE */
	CHECK(exists(book("Stopme.bin.part")));
	CHECK(db_row(st, &row) && row.state == DB_DL_ACTIVE && row.bytes > 0);

	memset(&row, 0, sizeof row); /* an ACTIVE row left by a crash, with 1000 bytes on disk */
	snprintf(row.url, sizeof row.url, "%s", U("/f/tall_800x9000.jpg"));
	snprintf(row.dest_path, sizeof row.dest_path, "%s", book("Resume.jpg"));
	snprintf(row.title, sizeof row.title, "Resume");
	row.state = DB_DL_ACTIVE;
	row.total = (int64_t)jlen;
	CHECK_INT(db_download_add(&row, &rs), 0);
	FILE *fp = fopen(book("Resume.jpg.part"), "wb");
	if (fp) fwrite(jpg, 1, 1000, fp), fclose(fp);
	get_tags(tags, sizeof tags);

	CHECK_INT(dlq_start(g_books), 0);
	CHECK(item(rs, &it) && it.state == DB_DL_QUEUED && it.bytes == 1000); /* Stopme runs first */
	CHECK_INT(dlq_add(U("/f/x.jpg"), NULL, "Resume", "jpg", &r2), 0); /* the .part holds the name */
	CHECK(item(r2, &it));
	CHECK_STR(it.dest_path, book("Resume (2).jpg"));
	CHECK_INT(dlq_cancel(r2), 0);
	CHECK_INT(dlq_cancel(st), 0);
	CHECK_INT(pump_idle(), 1);
	CHECK(item(rs, &it) && it.state == DB_DL_DONE);
	CHECK(file_is(book("Resume.jpg"), jpg, jlen));
	CHECK(!exists(book("Resume.jpg.part")) && !exists(book("Stopme.bin.part")));
	get_tags(tags, sizeof tags);
	CHECK_STR(tags, "range:1000");
	CHECK(db_row(rs, &row) && row.state == DB_DL_DONE && row.bytes == (int64_t)jlen);
	CHECK(db_row(st, &row) && row.state == DB_DL_CANCELLED);
	free(jpg);
}

int main(int argc, char **argv)
{
	char cmd[1400], dbp[700];
	int64_t id;
	(void)argc;
	CHECK_INT(dlq_add("http://127.0.0.1:1/", NULL, "x", "epub", &id), -1); /* not started */
	CHECK_INT(http_global_init(NULL), 0);
	CHECK_INT(server_start(argv[0], 11650), 0);
	test_names();
	snprintf(g_books, sizeof g_books, "%s/dlqtmp/books", g_build);
	snprintf(dbp, sizeof dbp, "%s/dlqtmp/q.db", g_build);
	snprintf(cmd, sizeof cmd, "rm -rf '%s/dlqtmp' && mkdir -p '%s'", g_build, g_books);
	CHECK_INT(system(cmd), 0);
	CHECK_INT(db_open(dbp), 0);
	CHECK_INT(dlq_start(g_books), 0);
	dlq_item_t all[4];
	CHECK_INT(dlq_list(all, 4), 0);
	CHECK(!dlq_busy());
	if (g_port) {
		test_queue();
		test_restart();
	}
	dlq_stop();
	db_close();
	server_stop();
	http_global_cleanup();
	return TEST_DONE("dlq");
}
