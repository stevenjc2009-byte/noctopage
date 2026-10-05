/* log.h - append-only log at ux0:data/noctopage/log.txt.
 * Every call is open("a") -> write -> close: Vita3K only flushes guest writes on
 * close, and a crash must not lose the lines before it.
 * Line format: "[%8.3f] message\n", seconds since process start. */
#ifndef NOCT_LOG_H
#define NOCT_LOG_H

/* Truncate the log (call once at boot so it holds only the current run). */
void log_reset(void);

void log_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
