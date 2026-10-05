/* third_party/mupdf/vita_compat.h — force-included (-include) into the Vita build
 * of MuPDF by tools/build_mupdf.sh. VitaSDK's newlib has no timegm(), which
 * source/pdf/pdf-parse.c (pdf_parse_date) calls. This is a static inline UTC
 * mktime so no symbol leaks into libmupdf.a. Not used by the host build. */
#ifndef NOCTOPAGE_MUPDF_VITA_COMPAT_H
#define NOCTOPAGE_MUPDF_VITA_COMPAT_H

#if !defined(__cplusplus)
#include <time.h>

static inline time_t noct_timegm(struct tm *tm)
{
	/* days_from_civil (H. Hinnant), with month normalised into the year. */
	long long y = (long long)tm->tm_year + 1900;
	long long m = tm->tm_mon;
	long long era, yoe, doy, doe, days;
	y += m / 12;
	m %= 12;
	if (m < 0) { m += 12; y -= 1; }
	m += 1; /* 1..12 */
	if (m <= 2) y -= 1;
	era = (y >= 0 ? y : y - 399) / 400;
	yoe = y - era * 400;
	doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + tm->tm_mday - 1;
	doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	days = era * 146097 + doe - 719468;
	return (time_t)(days * 86400LL + (long long)tm->tm_hour * 3600
		+ (long long)tm->tm_min * 60 + tm->tm_sec);
}
#define timegm noct_timegm
#endif

#endif
