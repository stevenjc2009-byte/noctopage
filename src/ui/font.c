/* UI text: vita2d_font (FreeType) on fonts embedded in MuPDF, so text renders at
 * the same pixel size on the Vita and in Vita3K.
 *   - The system PVF path was dropped: its size at a given "scale" is not
 *     documented, and Vita3K drew it about 4x larger than it measured.
 *   - Latin: MuPDF's Nimbus Sans (Helvetica metrics). CJK: MuPDF's Droid Sans
 *     Fallback (kept by TOFU_CJK_LANG). Both are static data inside libmupdf, so
 *     the buffers outlive the short-lived fz_context used to look them up.
 *   - Each code point goes to the Latin face when that face has a glyph for it,
 *     else to the CJK face; a string is drawn as runs of one face each. */
#include "ui/font.h"

#include "core/log.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include <mupdf/fitz.h>
#include <string.h>
#include <vita2d.h>

#define LINE_RATIO   1.25f /* line-box height / px size */
#define ASCENT_RATIO 0.93f /* baseline offset from the line top / px size (Nimbus/Droid) */
#define RUN_MAX      256

static vita2d_font *g_latin, *g_cjk;
static FT_Library g_ft;
static FT_Face g_latin_face; /* coverage checks only */

static const unsigned char *lookup(fz_context *ctx, int cjk, int *len)
{
	const unsigned char *d = NULL;
	fz_var(d);
	fz_try(ctx) {
		if (cjk) {
			int index = 0;
			d = fz_lookup_cjk_font(ctx, FZ_ADOBE_JAPAN, len, &index);
		} else {
			d = fz_lookup_base14_font(ctx, "Helvetica", len);
		}
	}
	fz_catch(ctx) d = NULL;
	return d;
}

void font_init(void)
{
	fz_context *ctx = fz_new_context(NULL, NULL, 0);
	if (!ctx) {
		log_printf("font: fz_new_context failed");
		return;
	}
	int llen = 0, clen = 0;
	const unsigned char *ldata = lookup(ctx, 0, &llen);
	const unsigned char *cdata = lookup(ctx, 1, &clen);
	fz_drop_context(ctx);

	if (ldata) {
		g_latin = vita2d_load_font_mem(ldata, (unsigned int)llen);
		if (FT_Init_FreeType(&g_ft) == 0 && FT_New_Memory_Face(g_ft, ldata, llen, 0, &g_latin_face) != 0)
			g_latin_face = NULL;
	}
	if (cdata)
		g_cjk = vita2d_load_font_mem(cdata, (unsigned int)clen);
	log_printf("font: latin %d bytes %s, cjk %d bytes %s", llen, g_latin ? "ok" : "FAILED", clen,
		   g_cjk ? "ok" : "FAILED");
}

/* Decode one UTF-8 sequence; returns its length (>= 1) and the code point. */
static int utf8_next(const char *s, unsigned int *cp)
{
	const unsigned char *u = (const unsigned char *)s;
	if (u[0] < 0x80) { *cp = u[0]; return 1; }
	if ((u[0] & 0xE0) == 0xC0 && (u[1] & 0xC0) == 0x80) {
		*cp = ((u[0] & 0x1Fu) << 6) | (u[1] & 0x3Fu);
		return 2;
	}
	if ((u[0] & 0xF0) == 0xE0 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80) {
		*cp = ((u[0] & 0x0Fu) << 12) | ((u[1] & 0x3Fu) << 6) | (u[2] & 0x3Fu);
		return 3;
	}
	if ((u[0] & 0xF8) == 0xF0 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80 && (u[3] & 0xC0) == 0x80) {
		*cp = ((u[0] & 0x07u) << 18) | ((u[1] & 0x3Fu) << 12) | ((u[2] & 0x3Fu) << 6) | (u[3] & 0x3Fu);
		return 4;
	}
	*cp = 0xFFFD;
	return 1;
}

static vita2d_font *face_for(unsigned int cp)
{
	if (!g_cjk)
		return g_latin;
	if (!g_latin)
		return g_cjk;
	if (cp < 0x80 || !g_latin_face)
		return g_latin;
	return FT_Get_Char_Index(g_latin_face, cp) ? g_latin : g_cjk;
}

/* Walk utf8 as runs of one face; draw each run at (x, baseline) when draw is set.
 * Returns the total advance width. */
static int runs(int x, int baseline, unsigned int color, int px, const char *utf8, int draw)
{
	char run[RUN_MAX];
	int width = 0;
	const char *p = utf8;
	while (*p) {
		unsigned int cp;
		utf8_next(p, &cp);
		vita2d_font *f = face_for(cp);
		int len = 0;
		while (*p) {
			int n = utf8_next(p, &cp);
			if (face_for(cp) != f || len + n >= RUN_MAX)
				break;
			memcpy(run + len, p, (size_t)n);
			len += n;
			p += n;
		}
		run[len] = '\0';
		if (!f || len == 0)
			continue;
		if (draw)
			vita2d_font_draw_text(f, x + width, baseline, color, (unsigned int)px, run);
		width += vita2d_font_text_width(f, (unsigned int)px, run);
	}
	return width;
}

void font_draw(int x, int y, unsigned int color, int px, const char *utf8)
{
	if (!utf8 || !*utf8)
		return;
	runs(x, y + (int)((float)px * ASCENT_RATIO), color, px, utf8, 1);
}

int font_width(int px, const char *utf8)
{
	if (!utf8 || !*utf8)
		return 0;
	return runs(0, 0, 0, px, utf8, 0);
}

int font_height(int px)
{
	return (int)((float)px * LINE_RATIO);
}
