/* net/fetch against a local Python server (tests/net_server.py, 127.0.0.1 only): newest-first
 * order, forget, cancel on stop, slot limit, image decode. */
#include "test.h"

#include "net/fetch.h"

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

/* Poll until the request leaves PENDING (10 s at most). */
static fetch_state_t wait_for(int id)
{
	fetch_state_t st = FETCH_PENDING;
	for (int i = 0; i < 1000 && (st = fetch_state(id)) == FETCH_PENDING; i++) usleep(10000);
	return st;
}

static void get_tags(char *out, size_t cap)
{
	http_doc_t d;
	char err[160];
	out[0] = '\0';
	if (http_get(U("/tags"), NULL, 0, NULL, &d, err, sizeof err) == 0) snprintf(out, cap, "%s", d.data);
	http_doc_free(&d);
}

static void test_basic(void)
{
	fetch_result_t r;
	size_t flen;
	char p[700];
	snprintf(p, sizeof p, "%s/fixtures/minimal.txt", g_tests);
	unsigned char *txt = read_all(p, &flen);

	int id = fetch_get(U("/f/minimal.txt"), NULL);
	CHECK(id > 0);
	CHECK_INT(wait_for(id), FETCH_DONE);
	CHECK_INT(fetch_take(id, &r), FETCH_DONE);
	CHECK(txt && r.doc.len == flen && memcmp(r.doc.data, txt, flen) == 0);
	CHECK_INT(r.doc.status, 200);
	CHECK_STR(r.doc.final_url, U("/f/minimal.txt"));
	CHECK(r.img.rgba == NULL);
	fetch_result_free(&r);
	CHECK_INT(fetch_state(id), FETCH_UNKNOWN);
	CHECK_INT(fetch_take(id, &r), FETCH_UNKNOWN);
	CHECK(r.doc.data == NULL && r.err[0] == '\0');
	fetch_result_free(&r);
	CHECK_INT(fetch_state(0), FETCH_UNKNOWN);
	CHECK_INT(fetch_state(id + 1000), FETCH_UNKNOWN);

	id = fetch_get(U("/404"), NULL);
	CHECK_INT(wait_for(id), FETCH_FAILED);
	CHECK_INT(fetch_take(id, &r), FETCH_FAILED);
	CHECK_STR(r.err, "HTTP 404");
	CHECK_INT(r.doc.status, 404);
	fetch_result_free(&r);

	id = fetch_get(U("/f/minimal.txt"), NULL); /* forgetting a finished request frees it */
	CHECK_INT(wait_for(id), FETCH_DONE);
	fetch_forget(id);
	CHECK_INT(fetch_state(id), FETCH_UNKNOWN);
	free(txt);
}

static void test_order(void)
{
	fetch_result_t r;
	char tags[256], user[16] = "user", pass[16] = "pass";
	http_auth_t a = { user, pass };
	get_tags(tags, sizeof tags);

	int slow = fetch_get(U("/slow?n=100&ms=100"), NULL); /* holds the worker */
	usleep(400000);
	int b = fetch_get(U("/tag/B"), NULL);
	int c = fetch_get(U("/tag/C"), NULL);
	int au = fetch_get(U("/auth/minimal.txt"), &a);
	strcpy(user, "xxxx"); /* the request must hold its own copy */
	strcpy(pass, "xxxx");
	int d = fetch_get(U("/tag/D"), NULL);
	CHECK(slow > 0 && b > slow && c > b && au > c && d > au);
	CHECK_INT(fetch_state(b), FETCH_PENDING);
	CHECK_INT(fetch_take(b, &r), FETCH_PENDING);
	CHECK(r.doc.data == NULL);
	fetch_forget(c);
	CHECK_INT(fetch_state(c), FETCH_UNKNOWN);
	double t0 = now_s();
	fetch_forget(slow); /* running: cancelled */
	CHECK_INT(fetch_state(slow), FETCH_UNKNOWN);
	CHECK_INT(wait_for(b), FETCH_DONE);
	CHECK(now_s() - t0 < 3.0);
	CHECK_INT(wait_for(d), FETCH_DONE);
	CHECK_INT(wait_for(au), FETCH_DONE);
	get_tags(tags, sizeof tags);
	CHECK_STR(tags, "D,B"); /* newest first, the forgotten C never ran */
	CHECK_INT(fetch_take(d, &r), FETCH_DONE);
	CHECK_STR(r.doc.data, "D");
	fetch_result_free(&r);
	CHECK(r.doc.data == NULL);
	fetch_forget(b);
	fetch_forget(au);
}

static void test_images(void)
{
	fetch_result_t r;
	int id = fetch_image(U("/f/tall_800x9000.png"), NULL, 100, 100);
	CHECK_INT(wait_for(id), FETCH_DONE);
	CHECK_INT(fetch_take(id, &r), FETCH_DONE);
	CHECK(r.img.rgba != NULL);
	CHECK_INT(r.img.h, 100);
	CHECK(r.img.w >= 8 && r.img.w <= 9);
	CHECK_INT(r.img.src_w, 800);
	CHECK_INT(r.img.src_h, 9000);
	CHECK(r.doc.data == NULL && r.doc.len == 0);
	CHECK_INT(r.doc.status, 200);
	fetch_result_free(&r);

	id = fetch_image(U("/redirect/tall_800x9000.jpg"), NULL, 0, 450);
	CHECK_INT(wait_for(id), FETCH_DONE);
	CHECK_INT(fetch_take(id, &r), FETCH_DONE);
	CHECK_INT(r.img.h, 450);
	CHECK_INT(r.img.w, 40);
	CHECK_STR(r.doc.final_url, U("/f/tall_800x9000.jpg"));
	fetch_result_free(&r);

	id = fetch_image(U("/f/minimal.txt"), NULL, 100, 100);
	CHECK_INT(wait_for(id), FETCH_FAILED);
	CHECK_INT(fetch_take(id, &r), FETCH_FAILED);
	CHECK(r.img.rgba == NULL && r.err[0] != '\0');
	fetch_result_free(&r);
}

static void test_slots_and_stop(void)
{
	int ids[FETCH_SLOTS], n = 0;
	ids[n++] = fetch_get(U("/slow?n=100&ms=100"), NULL);
	usleep(300000);
	while (n < FETCH_SLOTS) ids[n++] = fetch_get(U("/tag/x"), NULL);
	int all = 1;
	for (int i = 0; i < n; i++) all &= ids[i] > 0;
	CHECK(all);
	CHECK_INT(fetch_get(U("/tag/y"), NULL), -1);
	fetch_forget(ids[FETCH_SLOTS - 1]);
	int again = fetch_get(U("/tag/z"), NULL);
	CHECK(again > 0);
	double t0 = now_s();
	fetch_stop(); /* cancels the slow transfer, frees every queued request */
	CHECK(now_s() - t0 < 3.0);
	CHECK_INT(fetch_state(ids[1]), FETCH_UNKNOWN);
	CHECK_INT(fetch_get(U("/tag/y"), NULL), -1);
}

int main(int argc, char **argv)
{
	(void)argc;
	CHECK_INT(fetch_get("http://127.0.0.1:1/", NULL), -1); /* not started */
	CHECK_INT(http_global_init(NULL), 0);
	CHECK_INT(server_start(argv[0], 11630), 0);
	CHECK_INT(fetch_start(), 0);
	if (g_port) {
		test_basic();
		test_order();
		test_images();
		test_slots_and_stop();
	}
	fetch_stop();
	server_stop();
	http_global_cleanup();
	return TEST_DONE("fetch");
}
