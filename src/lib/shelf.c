#include "lib/shelf.h"
#include "core/strutil.h"
#include "core/natsort.h"
#include "core/casefold.h"
#include <string.h>
#include <ctype.h>

/* Return a bounded digit run after an explicit V/Vol/Volume marker. */
static const char *volume_number(const char *name, const char **marker)
{
	for (const char *p = name; *p; p++) {
		if (p > name && isalnum((unsigned char)p[-1])) continue;
		const char *q = p;
		if (tolower((unsigned char)*q++) != 'v') continue;
		if (tolower((unsigned char)*q) == 'o') {
			q++;
			if (tolower((unsigned char)*q++) != 'l') continue;
			if (tolower((unsigned char)*q) == 'u') {
				q++;
				if (tolower((unsigned char)*q++) != 'm' || tolower((unsigned char)*q++) != 'e') continue;
			}
		}
		while (*q == ' ' || *q == '.' || *q == '_' || *q == '-') q++;
		if (!isdigit((unsigned char)*q)) continue;
		const char *digits = q;
		while (isdigit((unsigned char)*q)) q++;
		if (*q && *q != '.' && *q != ' ' && *q != '_' && *q != '-' && *q != ']' && *q != ')') continue;
		if (marker) *marker = p;
		return digits;
	}
	return NULL;
}

/* Require a named prefix; root-level "Volume 1" is still a standalone book. */
static void filename_series(const char *name, char *out, size_t cap)
{
	str_copy(out, cap, "");
	const char *end;
	if (!volume_number(name, &end)) return;
	while (end > name && (end[-1] == ' ' || end[-1] == '_' || end[-1] == '-' || end[-1] == '[' || end[-1] == '(')) end--;
	size_t n = (size_t)(end - name);
	if (!n || !cap || n >= cap) return;
	memcpy(out, name, n); out[n] = 0;
}

void shelf_book_series(const db_book_t *book, char *out, size_t cap)
{
	if (book->series[0]) str_copy(out, cap, book->series);
	else shelf_series(book->path, out, cap);
}

void shelf_series(const char *path, char *out, size_t cap)
{
	const char *p = path;
	if (strncmp(p, "ux0:", 4)) { str_copy(out, cap, ""); return; }
	p += 4;
	while (*p == '/') p++;
	if (strncmp(p, "books/", 6)) { str_copy(out, cap, ""); return; }
	p += 6;
	const char *end = strrchr(p, '/');
	if (!end) { filename_series(p, out, cap); return; }
	size_t n = (size_t)(end - p);
	if (!cap) return;
	if (n >= cap) n = cap - 1;
	memcpy(out, p, n);
	out[n] = '\0';
}


int shelf_matches(const db_book_t *b, const char *q, int filter)
{
	if (b->missing) return 0;
	if (filter == SHELF_UNREAD && (b->last_read || b->finished)) return 0;
	if (filter == SHELF_READING && (!b->last_read || b->finished)) return 0;
	if (filter == SHELF_FINISHED && !b->finished) return 0;
	if (filter == SHELF_FAVORITES && !b->favorite) return 0;
	if (filter == SHELF_COMICS && b->kind != DB_KIND_ARCHIVE && b->kind != DB_KIND_FOLDER && b->kind != DB_KIND_FIXED) return 0;
	if (filter == SHELF_BOOKS && b->kind != DB_KIND_REFLOW && b->kind != DB_KIND_FIXED) return 0;
	return !q || !*q || unicode_contains_ci(b->title, q) || unicode_contains_ci(b->author, q) || unicode_contains_ci(b->path, q) || unicode_contains_ci(b->series, q);
}

int shelf_volume_compare(const db_book_t *a, const db_book_t *b)
{
	const char *an = path_basename(a->path), *bn = path_basename(b->path);
	const char *av = volume_number(an, NULL), *bv = volume_number(bn, NULL);
	/* A total order: numbered volumes precede unnumbered extras. Compare the
	 * digits without integer conversion, including arbitrarily large numbers. */
	if ((av != NULL) != (bv != NULL)) return av ? -1 : 1;
	if (av && bv) {
		while (*av == '0') av++;
		while (*bv == '0') bv++;
		size_t al = 0, bl = 0;
		while (isdigit((unsigned char)av[al])) al++;
		while (isdigit((unsigned char)bv[bl])) bl++;
		if (al != bl) return al < bl ? -1 : 1;
		int numeric = memcmp(av, bv, al);
		if (numeric) return numeric;
	}
	int c = natcmp(an, bn);
	return c ? c : (a->id > b->id) - (a->id < b->id);
}

int shelf_volume_index(const db_book_t *book)
{
 const char *name=path_basename(book->path),*p=volume_number(name,NULL);
 if(!p && isdigit((unsigned char)*name)) p=name;
 if(!p) return 0;
 int n=0; while(isdigit((unsigned char)*p)) { if(n>100) return 0; n=n*10+*p++-'0'; }
 return n>=1 && n<=1000 ? n:0;
}
