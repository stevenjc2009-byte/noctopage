/* fit — page placement maths for the reader (pure C, float).
 *
 * A page is pw x ph source pixels (for a double-page spread: both pages side by side).
 * The view shows it on a screen_w x screen_h screen (960x544 by default):
 *
 *   base scale  = fit_base_scale(mode, ...)
 *   scale       = base scale * zoom            (zoom clamped to FIT_ZOOM_MIN..FIT_ZOOM_MAX)
 *   shown size  = pw*scale x ph*scale
 *   top-left    = ((screen_w - shown_w)/2 + pan_x, (screen_h - shown_h)/2 + pan_y)
 *
 * pan is the offset of the page centre from the screen centre, in screen px (positive = page
 * moved right/down). Clamping per axis: if the shown page is larger than the screen the page
 * edges may not leave a gap (|pan| <= (shown - screen)/2); if it is smaller or equal, pan is
 * 0 (centred).
 *
 * Modes:
 *   FIT_WIDTH     scale = screen_w / pw
 *   FIT_HEIGHT    scale = screen_h / ph
 *   FIT_SMART     whole page (min of the two) unless ph/pw > FIT_SMART_RATIO (strictly), then
 *                 FIT_WIDTH. Exactly 2.0 tall still fits whole.
 *   FIT_ORIGINAL  scale = 1 (one source pixel per screen pixel)
 */
#ifndef NOCTO_FIT_H
#define NOCTO_FIT_H

#define FIT_ZOOM_MIN 1.0f
#define FIT_ZOOM_MAX 8.0f
#define FIT_SMART_RATIO 2.0f

typedef enum { FIT_WIDTH, FIT_HEIGHT, FIT_SMART, FIT_ORIGINAL } fit_mode_t;

typedef struct {
	int screen_w, screen_h;
	fit_mode_t mode;
	float zoom;
	float pan_x, pan_y;
} fit_view_t;

typedef struct {
	float x, y, w, h; /* destination rectangle in screen px */
} fit_rect_t;

/* Mode FIT_SMART, zoom 1, pan 0. */
void fit_view_init(fit_view_t *v, int screen_w, int screen_h);

float fit_base_scale(fit_mode_t mode, float pw, float ph, int screen_w, int screen_h);

/* Current total scale (base * zoom). */
float fit_scale(const fit_view_t *v, float pw, float ph);

/* Clamp zoom into FIT_ZOOM_MIN..FIT_ZOOM_MAX. */
float fit_clamp_zoom(float zoom);

/* Clamp v->pan_x/pan_y for this page (see header comment). */
void fit_clamp_pan(fit_view_t *v, float pw, float ph);

/* Destination rectangle of the page using the clamped pan (v is not modified). */
void fit_place(const fit_view_t *v, float pw, float ph, fit_rect_t *out);

/* Move the page by (dx,dy) screen px, then clamp. */
void fit_pan_by(fit_view_t *v, float pw, float ph, float dx, float dy);

/* Set zoom (clamped) so the page point under screen point (fx,fy) stays under it, then clamp
 * pan. The point only moves when the pan clamp forces it (page edge reached, or page smaller
 * than the screen). Pinch: pass zoom_at_pinch_start * gesture scale. */
void fit_set_zoom_at(fit_view_t *v, float pw, float ph, float zoom, float fx, float fy);

/* Page-space point (source px) under screen point (sx,sy), using the clamped pan. */
void fit_screen_to_page(const fit_view_t *v, float pw, float ph, float sx, float sy, float *px,
                        float *py);

/* Pan so the reading start of the page is on screen: top edge, and the leading side edge
 * (left for LTR, right for RTL). Axes where the page fits stay centred. The "end" variant
 * shows the bottom edge and trailing side (for stepping back onto the previous page). */
void fit_pan_start(fit_view_t *v, float pw, float ph, int rtl);
void fit_pan_end(fit_view_t *v, float pw, float ph, int rtl);

/* ---- vertical strip: pages stacked at fit-width scale ----------------------------------- */

typedef struct {
	float top;    /* y of the page top in strip space (0 = top of page 0) */
	float height; /* scaled page height */
} strip_item_t;

/* Scale of a strip page: zoom * screen_w / w. (zoom clamped like fit zoom.) */
float strip_scale(int page_w, int screen_w, float zoom);

/* Lay out n pages (w[i] x h[i]) one under another with `gap` px between them. A page with a
 * non-positive width or height gets height 0. Returns total height in screen px. */
float strip_layout(const int *w, const int *h, int n, int screen_w, float zoom, int gap,
                   strip_item_t *out);

/* Scroll position clamped to 0 .. max(0, total - screen_h). */
float strip_clamp_scroll(float total_height, int screen_h, float scroll);

/* Pages whose rectangle intersects the viewport [scroll, scroll + screen_h): first and last
 * page index (inclusive). Touching an edge is not intersecting; zero-height pages never
 * intersect. Returns the number of pages (0 if none; then both outputs are -1). */
int strip_visible(const strip_item_t *it, int n, float scroll, int screen_h, int *first,
                  int *last);

/* Index of the page containing strip-space y (the gap below a page belongs to it); y before
 * the strip gives 0, y past it n-1, -1 only when n <= 0. */
int strip_page_at(const strip_item_t *it, int n, float y);

#endif
