#include "lib/formats.h"

#include "core/strutil.h"

#include <string.h>

static const struct {
	const char *ext;
	fmt_kind_t kind;
} k_kinds[] = {
	{ "cbz", FMT_COMIC_ARCHIVE }, { "zip", FMT_COMIC_ARCHIVE }, { "cbr", FMT_COMIC_ARCHIVE },
	{ "rar", FMT_COMIC_ARCHIVE }, { "cb7", FMT_COMIC_ARCHIVE }, { "7z", FMT_COMIC_ARCHIVE },
	{ "cbt", FMT_COMIC_ARCHIVE }, { "tar", FMT_COMIC_ARCHIVE },
	{ "epub", FMT_REFLOW },       { "mobi", FMT_REFLOW },       { "fb2", FMT_REFLOW },
	{ "txt", FMT_REFLOW },        { "html", FMT_REFLOW },       { "htm", FMT_REFLOW },
	{ "xhtml", FMT_REFLOW },
	{ "pdf", FMT_FIXED },         { "xps", FMT_FIXED },         { "oxps", FMT_FIXED },
	{ "svg", FMT_FIXED },
};

static const char *const k_images[] = { "jpg", "jpeg", "png", "gif", "bmp", "webp", "tif", "tiff" };

fmt_kind_t formats_kind_for_path(const char *path)
{
	const char *ext;
	size_t i;

	if (!path)
		return FMT_NONE;
	ext = path_ext(path);
	for (i = 0; i < sizeof k_kinds / sizeof k_kinds[0]; i++)
		if (str_casecmp(ext, k_kinds[i].ext) == 0)
			return k_kinds[i].kind;
	return FMT_NONE;
}

int formats_is_image(const char *name)
{
	const char *ext;
	size_t i;

	if (!name)
		return 0;
	ext = path_ext(name);
	for (i = 0; i < sizeof k_images / sizeof k_images[0]; i++)
		if (str_casecmp(ext, k_images[i]) == 0)
			return 1;
	return 0;
}

const char *formats_kind_name(fmt_kind_t k)
{
	switch (k) {
	case FMT_COMIC_ARCHIVE: return "comic";
	case FMT_COMIC_FOLDER:  return "folder";
	case FMT_REFLOW:        return "book";
	case FMT_FIXED:         return "document";
	case FMT_NONE:
	default:                return "none";
	}
}

const char *formats_badge_r(const char *path, char *out, size_t cap)
{
	const char *ext;
	size_t n, i;

	if (cap == 0)
		return "";
	ext = path ? path_ext(path) : "";
	if (!*ext) {
		str_copy(out, cap, "DIR");
		return out;
	}
	n = strlen(ext);
	if (n > FORMATS_BADGE_MAX - 1)
		n = FORMATS_BADGE_MAX - 1;
	if (n > cap - 1)
		n = cap - 1;
	/* Do not cut a UTF-8 sequence in half: back off to a lead byte boundary. */
	while (n > 0 && (((unsigned char)ext[n]) & 0xC0) == 0x80)
		n--;
	for (i = 0; i < n; i++)
		out[i] = (ext[i] >= 'a' && ext[i] <= 'z') ? (char)(ext[i] - ('a' - 'A')) : ext[i];
	out[n] = '\0';
	return out;
}

const char *formats_badge(const char *path)
{
	static char buf[FORMATS_BADGE_MAX];
	return formats_badge_r(path, buf, sizeof buf);
}
