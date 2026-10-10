#ifndef NOCT_STICKZOOM_H
#define NOCT_STICKZOOM_H
/* Vita axis 0..255: up zooms in, down zooms out. Dead zone prevents drift.
 * Full tilt doubles/halves zoom per second; long frames capped at 50ms. */
float stickzoom_factor(int axis, unsigned milliseconds);
#endif
