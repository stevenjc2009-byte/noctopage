#include "core/autotest.h"

#include "core/log.h"
#include "core/paths.h"
#include "ui/input.h"

#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CMDS 256
#define CMD_LEN  96

static char g_cmds[MAX_CMDS][CMD_LEN];
static int g_ncmds;
static int g_idx;
static int g_active;
static uint64_t g_wait_until;
static uint64_t g_hold_until; /* a held button is released once now >= this */
static int g_holding;
static int g_settle;          /* one idle frame after a press so back-to-back presses re-trigger */

static const struct { const char *name; uint32_t bit; } NAMES[] = {
	{"cross", IN_CROSS}, {"circle", IN_CIRCLE}, {"square", IN_SQUARE}, {"triangle", IN_TRIANGLE},
	{"up", IN_UP}, {"down", IN_DOWN}, {"left", IN_LEFT}, {"right", IN_RIGHT},
	{"l", IN_L}, {"r", IN_R}, {"start", IN_START}, {"select", IN_SELECT},
};

static uint32_t button_by_name(const char *s)
{
	for (size_t i = 0; i < sizeof NAMES / sizeof NAMES[0]; i++)
		if (strcmp(NAMES[i].name, s) == 0)
			return NAMES[i].bit;
	return 0;
}

static void add_cmd(const char *s, size_t n)
{
	while (n && (*s == ' ' || *s == '\t')) { s++; n--; }
	while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) n--;
	if (n == 0 || s[0] == '#' || g_ncmds >= MAX_CMDS)
		return;
	if (n >= CMD_LEN) n = CMD_LEN - 1;
	memcpy(g_cmds[g_ncmds], s, n);
	g_cmds[g_ncmds][n] = '\0';
	g_ncmds++;
}

void autotest_init(void)
{
	FILE *f = fopen(PATH_AUTOTEST, "r");
	if (!f)
		return;
	char buf[8192];
	size_t len = fread(buf, 1, sizeof buf - 1, f);
	fclose(f);
	buf[len] = '\0';
	sceIoRename(PATH_AUTOTEST, PATH_AUTOTEST_DONE);

	/* Split on newlines and ';'. */
	char *start = buf;
	for (char *p = buf;; p++) {
		if (*p == '\n' || *p == ';' || *p == '\0') {
			add_cmd(start, (size_t)(p - start));
			start = p + 1;
			if (*p == '\0')
				break;
		}
	}
	g_active = 1;
	log_printf("AUTOTEST start (%d commands)", g_ncmds);
}

int autotest_active(void)
{
	return g_active;
}

static void run_cmd(const char *cmd, uint64_t now_ms)
{
	char verb[16] = "";
	char arg[CMD_LEN] = "";
	sscanf(cmd, "%15s %95[^\n]", verb, arg);

	if (strcmp(verb, "wait") == 0) {
		g_wait_until = now_ms + (uint64_t)strtoul(arg, NULL, 10);
	} else if (strcmp(verb, "press") == 0) {
		uint32_t b = button_by_name(arg);
		if (!b)
			log_printf("AUTOTEST bad button '%s'", arg);
		input_inject_set(b);
		g_settle = 1;
	} else if (strcmp(verb, "hold") == 0) {
		char name[16] = "";
		unsigned long ms = 0;
		sscanf(arg, "%15s %lu", name, &ms);
		uint32_t b = button_by_name(name);
		if (!b)
			log_printf("AUTOTEST bad button '%s'", name);
		input_inject_set(b);
		g_holding = 1;
		g_hold_until = now_ms + ms;
	} else if (strcmp(verb, "shot") == 0) {
		log_printf("AUTOTEST shot %s", arg);
		g_wait_until = now_ms + 1500;
	} else if (strcmp(verb, "log") == 0) {
		log_printf("AUTOTEST log %s", arg);
	} else if (strcmp(verb, "exit") == 0) {
		log_printf("exit");
		sceKernelExitProcess(0); /* no vita2d_fini / font free: they hang under Vita3K */
	} else {
		log_printf("AUTOTEST unknown command '%s'", cmd);
	}
}

void autotest_tick(uint64_t now_ms)
{
	if (!g_active)
		return;

	if (g_holding) {
		if (now_ms < g_hold_until)
			return;
		g_holding = 0;
		input_inject_set(0);
		g_settle = 1;
		return;
	}
	if (g_settle) { /* the previous press has been seen as down; now let it be seen as up */
		g_settle = 0;
		input_inject_set(0);
		return;
	}
	if (now_ms < g_wait_until)
		return;

	/* Run commands until one blocks (wait / press / hold / shot) or the script ends. */
	while (g_idx < g_ncmds) {
		const char *cmd = g_cmds[g_idx++];
		run_cmd(cmd, now_ms);
		if (g_settle || g_holding || g_wait_until > now_ms)
			return;
	}
	if (g_idx >= g_ncmds) {
		g_active = 0;
		log_printf("AUTOTEST end of script");
	}
}
