/* text.h - word wrap and ellipsis for UI labels (UTF-8, CJK-aware). */
#ifndef NOCT_TEXT_H
#define NOCT_TEXT_H

#define TEXT_LINE_MAX 192 /* bytes per wrapped line, including the NUL */

/* Wraps utf8 into at most max_lines lines no wider than max_w pixels at size px.
 * Breaks at spaces, or between any two characters when one is CJK (>= U+2E80) or
 * a single word is wider than the line. The last line ends in "…" when text was
 * cut. Returns the number of lines written (0 for empty text). */
int text_wrap(int px, const char *utf8, int max_w, int max_lines, char lines[][TEXT_LINE_MAX]);

/* One line, cut with "…" to fit max_w. */
void text_ellipsize(int px, const char *utf8, int max_w, char out[TEXT_LINE_MAX]);

#endif
