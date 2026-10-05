/* osk.c - sceImeDialog wrapper; see osk.h. Adapted from vita-radio src/ime.c. */
#include "ui/osk.h"

#include <psp2/apputil.h>
#include <psp2/common_dialog.h>
#include <psp2/ime_dialog.h>
#include <psp2/sysmodule.h>
#include <psp2/system_param.h>
#include <stdint.h>
#include <string.h>
#include <vita2d.h>

#define TITLE_MAX SCE_IME_DIALOG_MAX_TITLE_LENGTH

/* The dialog writes into the input buffer while it is up, so these outlive a frame. */
static SceWChar16 g_title[TITLE_MAX + 1];
static SceWChar16 g_initial[OSK_MAX_TEXT + 1];
static SceWChar16 g_input[OSK_MAX_TEXT + 1];
static int g_ready, g_busy;

/* UTF-8 to UTF-16, at most cap - 1 units plus a terminator; malformed, overlong and
 * surrogate code points are skipped. */
static void to_utf16(const char *src, SceWChar16 *dst, size_t cap)
{
	const unsigned char *s = (const unsigned char *)(src ? src : "");
	size_t o = 0;
	while (*s && o + 1 < cap) {
		unsigned char c = s[0];
		uint32_t cp;
		size_t n;
		if (c < 0x80) {
			cp = c;
			n = 1;
		} else if ((c & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
			cp = ((uint32_t)(c & 0x1F) << 6) | (s[1] & 0x3Fu);
			n = 2;
		} else if ((c & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
			cp = ((uint32_t)(c & 0x0F) << 12) | ((s[1] & 0x3Fu) << 6) | (s[2] & 0x3Fu);
			n = 3;
		} else if ((c & 0xF8) == 0xF0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 &&
		           (s[3] & 0xC0) == 0x80) {
			cp = ((uint32_t)(c & 0x07) << 18) | ((s[1] & 0x3Fu) << 12) | ((s[2] & 0x3Fu) << 6) |
			     (s[3] & 0x3Fu);
			n = 4;
		} else {
			s++;
			continue;
		}
		s += n;
		if ((n == 2 && cp < 0x80) || (n == 3 && cp < 0x800) || (n == 4 && cp < 0x10000))
			continue;
		if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) continue;
		if (cp >= 0x10000) {
			if (o + 2 >= cap) break;
			cp -= 0x10000;
			dst[o++] = (SceWChar16)(0xD800 | (cp >> 10));
			dst[o++] = (SceWChar16)(0xDC00 | (cp & 0x3FF));
		} else {
			dst[o++] = (SceWChar16)cp;
		}
	}
	dst[o] = 0;
}

/* UTF-16 to UTF-8, at most cap - 1 bytes plus a terminator; unpaired surrogates skipped. */
static void to_utf8(const SceWChar16 *src, char *dst, size_t cap)
{
	size_t i = 0, o = 0;
	while (src[i]) {
		uint32_t cp = src[i++];
		if (cp >= 0xD800 && cp <= 0xDBFF) {
			uint32_t lo = src[i];
			if (lo < 0xDC00 || lo > 0xDFFF) continue;
			cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
			i++;
		} else if (cp >= 0xDC00 && cp <= 0xDFFF) {
			continue;
		}
		size_t n = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
		if (o + n >= cap) break;
		if (n == 1) {
			dst[o++] = (char)cp;
		} else if (n == 2) {
			dst[o++] = (char)(0xC0 | (cp >> 6));
			dst[o++] = (char)(0x80 | (cp & 0x3F));
		} else if (n == 3) {
			dst[o++] = (char)(0xE0 | (cp >> 12));
			dst[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
			dst[o++] = (char)(0x80 | (cp & 0x3F));
		} else {
			dst[o++] = (char)(0xF0 | (cp >> 18));
			dst[o++] = (char)(0x80 | ((cp >> 12) & 0x3F));
			dst[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
			dst[o++] = (char)(0x80 | (cp & 0x3F));
		}
	}
	dst[o] = '\0';
}

int osk_init(void)
{
	if (g_ready) return 0;
	/* Loading a module that is already up also reports an error: not fatal. */
	sceSysmoduleLoadModule(SCE_SYSMODULE_IME);
	SceAppUtilInitParam init;
	SceAppUtilBootParam boot;
	memset(&init, 0, sizeof init);
	memset(&boot, 0, sizeof boot);
	sceAppUtilInit(&init, &boot); /* input_init already did; a repeat fails harmlessly */
	int lang = SCE_SYSTEM_PARAM_LANG_ENGLISH_US, enter = SCE_SYSTEM_PARAM_ENTER_BUTTON_CROSS;
	sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &lang);
	sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_ENTER_BUTTON, &enter);
	SceCommonDialogConfigParam config;
	sceCommonDialogConfigParamInit(&config);
	config.language = (SceSystemParamLang)lang;
	config.enterButtonAssign = (SceSystemParamEnterButtonAssign)enter;
	if (sceCommonDialogSetConfigParam(&config) < 0) return -1;
	g_ready = 1;
	return 0;
}

int osk_prompt(const char *title, const char *initial, osk_kind_t kind, char *out, size_t cap)
{
	if (!g_ready || !out || cap == 0 || g_busy) return -1;
	memset(g_title, 0, sizeof g_title);
	memset(g_initial, 0, sizeof g_initial);
	memset(g_input, 0, sizeof g_input);
	to_utf16(title, g_title, TITLE_MAX + 1);
	to_utf16(initial, g_initial, OSK_MAX_TEXT + 1);

	SceImeDialogParam p;
	sceImeDialogParamInit(&p);
	/* Type, languages and forcing left at their defaults: the keyboard follows the
	 * languages the user enabled, so accented and Japanese titles can be searched. */
	p.type = kind == OSK_URL ? SCE_IME_TYPE_URL : SCE_IME_TYPE_DEFAULT;
	p.option = 0;
	p.dialogMode = SCE_IME_DIALOG_DIALOG_MODE_DEFAULT;
	p.textBoxMode = kind == OSK_PASSWORD ? SCE_IME_DIALOG_TEXTBOX_MODE_PASSWORD
	                                     : SCE_IME_DIALOG_TEXTBOX_MODE_DEFAULT;
	p.enterLabel = SCE_IME_ENTER_LABEL_DEFAULT;
	p.maxTextLength = OSK_MAX_TEXT;
	p.title = g_title;
	p.initialText = g_initial;
	p.inputTextBuffer = g_input;
	if (sceImeDialogInit(&p) < 0) return -1;
	g_busy = 1;

	for (;;) {
		vita2d_start_drawing();
		vita2d_clear_screen();
		vita2d_draw_rectangle(0, 0, 960, 544, RGBA8(0, 0, 0, 170));
		vita2d_end_drawing();
		vita2d_common_dialog_update();
		vita2d_swap_buffers();
		SceCommonDialogStatus st = sceImeDialogGetStatus();
		if (st == SCE_COMMON_DIALOG_STATUS_FINISHED) break;
		if (st == SCE_COMMON_DIALOG_STATUS_NONE) { /* went away without finishing */
			sceImeDialogTerm();
			g_busy = 0;
			return -1;
		}
	}
	SceImeDialogResult r;
	memset(&r, 0, sizeof r);
	sceImeDialogGetResult(&r);
	sceImeDialogTerm();
	g_busy = 0;
	if (r.button != SCE_IME_DIALOG_BUTTON_ENTER) return 0;
	to_utf8(g_input, out, cap);
	return 1;
}

void osk_shutdown(void)
{
	if (g_busy) {
		sceImeDialogTerm();
		g_busy = 0;
	}
	sceAppUtilShutdown();
	sceSysmoduleUnloadModule(SCE_SYSMODULE_IME);
	g_ready = 0;
}
