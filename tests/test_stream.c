/* OPDS-PSE streaming (reader/loader_stream via loader_open_stream) against the local
 * Python server: page count without download, page fetch + decode, disk cache reuse
 * with the server dead, bad arguments. Server helper copied from test_fetch.c. */
#include "test.h"

#include "core/fnv.h"
#include "net/http.h"
#include "reader/loader.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <libgen.h>
#include <limits.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

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
	g_srv = 0;
}

/* Remove this test's stream cache directory (tests run with CWD = tests/). */
static void cache_wipe(const char *tmpl)
{
	char hex[17], dir[256], p[512];
	DIR *d;
	struct dirent *e;
	fnv64_hex(fnv64(tmpl, strlen(tmpl)), hex);
	snprintf(dir, sizeof dir, "ux0:data/noctopage/stream/%s", hex);
	d = opendir(dir);
	if (!d) return;
	while ((e = readdir(d)) != NULL) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
		snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
		remove(p);
	}
	closedir(d);
	rmdir(dir);
}

static int wait_ready(loader_t *l)
{
	char err[160];
	for (int i = 0; i < 100; i++) {
		loader_status_t s = loader_status(l, err, sizeof err);
		if (s == LOADER_READY) return 1;
		if (s == LOADER_FAILED) return 0;
		usleep(50000);
	}
	return 0;
}

/* Wish for page and wait until it is uploaded-ready; returns 1 with w/h filled. */
static int wait_page(loader_t *l, int page, int *w, int *h)
{
	loader_req_t r = { page, 960, 16384 };
	loader_want(l, &r, 1);
	for (int i = 0; i < 600; i++) {
		const loader_page_t *p = loader_get(l, page, 960, 16384);
		if (p) {
			*w = p->w;
			*h = p->h;
			loader_release(l, p);
			return 1;
		}
		usleep(50000);
	}
	return 0;
}

int main(int argc, char **argv)
{
	char tmpl[256];
	loader_t *l;
	int w, h;
	(void)argc;

	/* Bad arguments never start a loader. */
	CHECK(loader_open_stream(NULL, 3, "", "") == NULL);
	CHECK(loader_open_stream("", 3, "", "") == NULL);
	CHECK(loader_open_stream("http://x/{pageNumber}", 0, "", "") == NULL);
	CHECK(loader_open_stream("http://x/{pageNumber}", -2, "", "") == NULL);

	CHECK(http_global_init(NULL) == 0);
	CHECK(server_start(argv[0], 11640) == 0);
	snprintf(tmpl, sizeof tmpl, "http://127.0.0.1:%d/f/tall_800x9000.png?page={pageNumber}", g_port);

	l = loader_open_stream(tmpl, 3, "", "");
	CHECK(l != NULL);
	if (!l) return TEST_DONE("stream");
	CHECK(wait_ready(l));
	CHECK_INT(loader_page_count(l), 3);
	/* 800x9000 in a 960x16384 box: natural size, never upscaled. */
	CHECK(wait_page(l, 0, &w, &h) && w == 800 && h == 9000);
	CHECK(wait_page(l, 2, &w, &h) && w == 800 && h == 9000);
	loader_close(l);

	/* The disk cache serves pages with the server dead. */
	server_stop();
	l = loader_open_stream(tmpl, 3, "", "");
	CHECK(l != NULL && wait_ready(l));
	CHECK(wait_page(l, 0, &w, &h) && w == 800 && h == 9000);
	CHECK(wait_page(l, 2, &w, &h) && w == 800 && h == 9000);
	loader_close(l);
	cache_wipe(tmpl);

	http_global_cleanup();
	return TEST_DONE("stream");
}
