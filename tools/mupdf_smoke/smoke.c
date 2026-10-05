/* tools/mupdf_smoke/smoke.c — MuPDF link + render smoke test (host and Vita).
 *
 * Builds three documents in memory (XHTML with Japanese text, SVG, a hand-written
 * PDF with exact xref offsets), lays each out at 960x544 em 12, counts pages,
 * renders page 1 to an RGB pixmap and prints dimensions + an FNV-1a checksum.
 * Also checks the embedded base-14 and CJK fallback fonts are present.
 * Exits non-zero on any failure. On the Vita it is only linked (vita-elf-create
 * proves there are no unresolved imports); it is run on the host.
 */
#include <mupdf/fitz.h>
#include <stdio.h>
#include <string.h>

#if FZ_ENABLE_JS != 0 || FZ_ENABLE_BROTLI != 0
#error "installed config.h does not carry the Noctopage build configuration"
#endif

static const char xhtml_doc[] =
	"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
	"<html xmlns=\"http://www.w3.org/1999/xhtml\" xml:lang=\"ja\"><head><title>smoke</title></head>"
	"<body><h1>Noctopage smoke</h1><p>The quick brown fox jumps over the lazy dog.</p>"
	"<p>\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\xE3\x81\xAE\xE3\x83\x86\xE3\x82\xAD\xE3\x82\xB9\xE3\x83\x88"
	" (\xE3\x81\xB2\xE3\x82\x89\xE3\x81\x8C\xE3\x81\xAA\xE3\x80\x81\xE3\x82\xAB\xE3\x82\xBF\xE3\x82\xAB\xE3\x83\x8A)</p>"
	"</body></html>\n";

static const char svg_doc[] =
	"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"200\" height=\"100\">"
	"<rect x=\"10\" y=\"10\" width=\"120\" height=\"60\" fill=\"#4169e2\"/>"
	"<circle cx=\"160\" cy=\"50\" r=\"30\" fill=\"#891e2b\"/></svg>\n";

static unsigned int fnv1a(const unsigned char *p, size_t n)
{
	unsigned int h = 2166136261u;
	size_t i;
	for (i = 0; i < n; i++)
	{
		h ^= p[i];
		h *= 16777619u;
	}
	return h;
}

/* Build a minimal one-page PDF (Helvetica text + a filled rectangle) with a
 * correct xref table, so the parser is exercised without repair. */
static fz_buffer *make_pdf(fz_context *ctx)
{
	static const char content[] =
		"0.2 0.4 0.9 rg 72 600 200 100 re f\n"
		"BT /F1 24 Tf 72 720 Td (Hello from Noctopage) Tj ET\n";
	const char *objs[5];
	char obj4[256];
	size_t offs[6];
	fz_buffer *buf = fz_new_buffer(ctx, 2048);
	int i;

	objs[0] = "<< /Type /Catalog /Pages 2 0 R >>";
	objs[1] = "<< /Type /Pages /Kids [3 0 R] /Count 1 >>";
	objs[2] = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792]"
		" /Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>";
	snprintf(obj4, sizeof obj4, "<< /Length %d >>\nstream\n%sendstream", (int)strlen(content), content);
	objs[3] = obj4;
	objs[4] = "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>";

	fz_try(ctx)
	{
		fz_append_string(ctx, buf, "%PDF-1.4\n");
		for (i = 0; i < 5; i++)
		{
			offs[i + 1] = buf->len;
			fz_append_printf(ctx, buf, "%d 0 obj\n%s\nendobj\n", i + 1, objs[i]);
		}
		offs[0] = buf->len;
		fz_append_string(ctx, buf, "xref\n0 6\n0000000000 65535 f \n");
		for (i = 1; i <= 5; i++)
			fz_append_printf(ctx, buf, "%010d 00000 n \n", (int)offs[i]);
		fz_append_printf(ctx, buf, "trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n", (int)offs[0]);
	}
	fz_catch(ctx)
	{
		fz_drop_buffer(ctx, buf);
		fz_rethrow(ctx);
	}
	return buf;
}

/* Open, lay out, count, render page 1. Returns 0 on success. */
static int check_doc(fz_context *ctx, const char *name, const char *magic, fz_buffer *buf)
{
	fz_document *doc = NULL;
	fz_pixmap *pix = NULL;
	int pages = 0, rc = 1;

	fz_var(doc);
	fz_var(rc);
	fz_var(pix);
	fz_try(ctx)
	{
		size_t x, y, nonwhite = 0, stride;
		doc = fz_open_document_with_buffer(ctx, magic, buf);
		fz_layout_document(ctx, doc, 960, 544, 12);
		pages = fz_count_pages(ctx, doc);
		if (pages < 1)
			fz_throw(ctx, FZ_ERROR_GENERIC, "no pages");
		pix = fz_new_pixmap_from_page_number(ctx, doc, 0, fz_identity, fz_device_rgb(ctx), 0);
		stride = (size_t)pix->stride;
		for (y = 0; y < (size_t)pix->h; y++)
		{
			const unsigned char *row = pix->samples + y * stride;
			for (x = 0; x < (size_t)pix->w * pix->n; x += pix->n)
				if (row[x] != 255 || row[x + 1] != 255 || row[x + 2] != 255)
					nonwhite++;
		}
		printf("%-5s pages=%d page1=%dx%d n=%d nonwhite=%zu fnv1a=%08x\n",
			name, pages, pix->w, pix->h, pix->n, nonwhite,
			fnv1a(pix->samples, stride * (size_t)pix->h));
		if (pix->n != 3 || nonwhite == 0)
			fz_throw(ctx, FZ_ERROR_GENERIC, "bad render (n=%d nonwhite=%zu)", pix->n, nonwhite);
		rc = 0;
	}
	fz_always(ctx)
	{
		fz_drop_pixmap(ctx, pix);
		fz_drop_document(ctx, doc);
	}
	fz_catch(ctx)
	{
		fprintf(stderr, "FAIL %s: %s\n", name, fz_caught_message(ctx));
	}
	return rc;
}

int main(void)
{
	fz_context *ctx;
	fz_buffer *buf = NULL;
	int failures = 0, len = 0, sub = 0;

	ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
	if (!ctx)
	{
		fprintf(stderr, "FAIL: cannot create fz_context\n");
		return 1;
	}
	printf("MuPDF %s\n", FZ_VERSION);

	fz_var(buf);
	fz_var(failures);
	fz_try(ctx)
		fz_register_document_handlers(ctx);
	fz_catch(ctx)
	{
		fprintf(stderr, "FAIL register handlers: %s\n", fz_caught_message(ctx));
		fz_drop_context(ctx);
		return 1;
	}

	/* Embedded fonts: base-14 must exist; the Japanese fallback must exist. */
	if (!fz_lookup_base14_font(ctx, "Times-Roman", &len) || len <= 0)
	{
		fprintf(stderr, "FAIL: base-14 Times-Roman missing\n");
		failures++;
	}
	else
		printf("font  base14 Times-Roman bytes=%d\n", len);
	len = 0;
	if (!fz_lookup_cjk_font(ctx, FZ_ADOBE_JAPAN, &len, &sub) || len <= 0)
	{
		fprintf(stderr, "FAIL: CJK (Japan) fallback font missing\n");
		failures++;
	}
	else
		printf("font  cjk japan bytes=%d subfont=%d\n", len, sub);

	fz_try(ctx)
	{
		buf = fz_new_buffer_from_copied_data(ctx, (const unsigned char *)xhtml_doc, strlen(xhtml_doc));
		failures += check_doc(ctx, "xhtml", "application/xhtml+xml", buf);
		fz_drop_buffer(ctx, buf);
		buf = NULL;

		buf = fz_new_buffer_from_copied_data(ctx, (const unsigned char *)svg_doc, strlen(svg_doc));
		failures += check_doc(ctx, "svg", "image/svg+xml", buf);
		fz_drop_buffer(ctx, buf);
		buf = NULL;

		buf = make_pdf(ctx);
		failures += check_doc(ctx, "pdf", "application/pdf", buf);
	}
	fz_always(ctx)
		fz_drop_buffer(ctx, buf);
	fz_catch(ctx)
	{
		fprintf(stderr, "FAIL: %s\n", fz_caught_message(ctx));
		failures++;
	}

	fz_drop_context(ctx);
	printf("%s (%d failure%s)\n", failures ? "SMOKE FAIL" : "SMOKE PASS", failures, failures == 1 ? "" : "s");
	return failures ? 1 : 0;
}
