/* book_draw.c - the book reader's page textures and drawing: the page (or a status line
 * while it lays out), the dim layer and the overlay (title, page counter, settings line,
 * page slider). Textures are created and freed in the update step only (book_tex_fetch);
 * drawing only looks them up. */
#include "screens/book_priv.h"

#include "core/log.h"
#include "ui/draw.h"
#include "ui/font.h"
#include "ui/theme.h"

#include <stdio.h>
#include <string.h>
#include <vita2d.h>

#define SLOTS 4
#define BAR_H 80
#define SLIDER_X0 70
#define SLIDER_X1 890
#define SLIDER_Y (544 - BAR_H + 26)

typedef struct {
	vita2d_texture *tex;
	int gen, page;
	uint32_t ink, paper;
	char pos[MDOC_POS_MAX];
	uint32_t tick;
} slot_t;

static slot_t g_slot[SLOTS];
static uint32_t g_tick;

static void free_slot(slot_t *s)
{
	if (!s->tex) return;
	vita2d_wait_rendering_done();
	vita2d_free_texture(s->tex);
	memset(s, 0, sizeof *s);
}

void book_tex_clear(void)
{
	for (int i = 0; i < SLOTS; i++) free_slot(&g_slot[i]);
}

static slot_t *find(int page)
{
	for (int i = 0; i < SLOTS; i++) {
		slot_t *s = &g_slot[i];
		if (s->tex && s->gen == B.gen && s->page == page && s->ink == book_ink() &&
		    s->paper == book_paper())
			return s;
	}
	return NULL;
}

void *book_tex_peek(int page)
{
	slot_t *s = find(page);
	return s ? s->tex : NULL;
}

void *book_tex_fetch(int page)
{
	if (page < 0 || page >= B.count) return NULL;
	slot_t *s = find(page);
	if (!s) {
		const bookld_page_t *p = bookld_get(B.bl, B.gen, page, book_ink(), book_paper());
		if (!p) return NULL;
		s = &g_slot[0];
		for (int i = 0; i < SLOTS; i++) {
			if (!g_slot[i].tex) {
				s = &g_slot[i];
				break;
			}
			if (g_slot[i].tick < s->tick) s = &g_slot[i];
		}
		free_slot(s);
		s->tex = vita2d_create_empty_texture((unsigned)p->w, (unsigned)p->h);
		if (s->tex) {
			uint8_t *dst = vita2d_texture_get_datap(s->tex);
			unsigned stride = vita2d_texture_get_stride(s->tex);
			for (int y = 0; y < p->h; y++)
				memcpy(dst + (size_t)y * stride, p->rgba + (size_t)y * (size_t)p->w * 4,
				       (size_t)p->w * 4);
			s->gen = p->gen;
			s->page = p->page;
			s->ink = p->ink;
			s->paper = p->paper;
			snprintf(s->pos, sizeof s->pos, "%s", p->pos);
		} else {
			log_printf("book: out of GPU memory for page %d", page);
		}
		bookld_release(B.bl, p);
		if (!s->tex) return NULL;
	}
	s->tick = ++g_tick;
	if (page == B.page && !B.pos[0]) snprintf(B.pos, sizeof B.pos, "%s", s->pos);
	return s->tex;
}

static unsigned rgb(uint32_t c)
{
	return RGBA8((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, 255);
}

static void centred(int y, unsigned colour, int px, const char *s)
{
	font_draw((960 - font_width(px, s)) / 2, y, colour, px, s);
}

void book_draw_page(void)
{
	unsigned paper = rgb(book_paper()), ink = rgb(book_ink());
	draw_rect(0, 0, 960, 544, paper);
	if (B.info.status == BOOKLD_FAILED) {
		draw_rect(0, 0, 960, 544, THEME_BLACK);
		centred(544 / 2 - 30, THEME_FONT, FONT_PX_TITLE, "This book could not be opened");
		centred(544 / 2 + 4, THEME_SUBFONT, FONT_PX_LABEL, B.info.err);
		return;
	}
	vita2d_texture *t = B.count > 0 ? book_tex_peek(B.page) : NULL;
	if (t) {
		vita2d_draw_texture(t, 0, 0);
	} else {
		char msg[64];
		if (B.count <= 0)
			snprintf(msg, sizeof msg, "Opening\xE2\x80\xA6");
		else if (B.page == B.failed_page)
			snprintf(msg, sizeof msg, "Page %d could not be shown", B.page + 1);
		else
			snprintf(msg, sizeof msg, "Page %d", B.page + 1);
		centred(544 / 2 - 12, ink, FONT_PX_TITLE, msg);
	}
	if (B.info.busy) centred(544 - 40, ink, FONT_PX_LABEL, "Laying out\xE2\x80\xA6");
	if (B.dim > 0) draw_rect(0, 0, 960, 544, RGBA8(0, 0, 0, B.dim * 64));
}

static int slider_x(int page)
{
	float f = B.count > 1 ? (float)page / (float)(B.count - 1) : 0.0f;
	return SLIDER_X0 + (int)(f * (SLIDER_X1 - SLIDER_X0));
}

int book_slider_page(int x)
{
	float f = (float)(x - SLIDER_X0) / (float)(SLIDER_X1 - SLIDER_X0);
	if (f < 0) f = 0;
	if (f > 1) f = 1;
	return (int)(f * (float)(B.count - 1) + 0.5f);
}

int book_slider_hit(int x, int y)
{
	(void)x;
	return B.overlay && B.count > 1 && y >= 544 - BAR_H && y < 544 - 23;
}

void book_draw_overlay(void)
{
	if (!B.overlay) return;
	char buf[128];
	draw_rect(0, 0, 960, BAR_H, RGBA8(0, 0, 0, 224));
	draw_rect(0, 544 - BAR_H, 960, BAR_H, RGBA8(0, 0, 0, 224));
	int shown = (B.seeking ? B.seek_page : B.page) + 1;
	snprintf(buf, sizeof buf, "%d / %d", shown, B.count);
	int cw = font_width(FONT_PX_TITLE, buf);
	font_draw(960 - 24 - cw, 14, THEME_FONT, FONT_PX_TITLE, buf);
	char title[sizeof B.book.title + 4];
	snprintf(title, sizeof title, "%s", B.book.title);
	int room = 960 - 24 - cw - 48;
	for (int n = (int)strlen(title); n > 0 && font_width(FONT_PX_TITLE, title) > room; n--) {
		while (n > 0 && ((unsigned char)title[n] & 0xC0) == 0x80) n--; /* whole UTF-8 chars */
		snprintf(title + n, sizeof title - (size_t)n, "\xE2\x80\xA6");
	}
	font_draw(24, 14, THEME_FONT, FONT_PX_TITLE, title);
	snprintf(buf, sizeof buf, "%d px  \xC2\xB7  Margins %d  \xC2\xB7  Spacing %.1f  \xC2\xB7  %s  \xC2\xB7  Dim %d%%",
	         B.style.font_px, B.style.margin_px, (double)B.style.line_spacing, book_theme_name(),
	         B.dim * 25);
	font_draw(24, 46, THEME_SUBFONT, FONT_PX_LABEL, buf);
	if (B.count > 1) {
		int kx = slider_x(B.seeking ? B.seek_page : B.page);
		draw_rect(SLIDER_X0, SLIDER_Y - 2, SLIDER_X1 - SLIDER_X0, 4, THEME_PANEL);
		draw_rect(SLIDER_X0, SLIDER_Y - 2, kx - SLIDER_X0, 4, THEME_SELECTOR);
		draw_rrect(kx - 8, SLIDER_Y - 8, 16, 16, 8, THEME_FONT);
		font_draw(SLIDER_X0 - 12 - font_width(FONT_PX_LABEL, "1"), SLIDER_Y - 10, THEME_SUBFONT,
		          FONT_PX_LABEL, "1");
		snprintf(buf, sizeof buf, "%d", B.count);
		font_draw(SLIDER_X1 + 12, SLIDER_Y - 10, THEME_SUBFONT, FONT_PX_LABEL, buf);
	}
}
