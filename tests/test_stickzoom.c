#include "test.h"
#include "reader/stickzoom.h"
#include "reader/fit.h"
#include <math.h>
int main(void)
{
    CHECK(stickzoom_factor(128, 16) == 1.0f);
    CHECK(stickzoom_factor(100, 16) == 1.0f);
    CHECK(stickzoom_factor(156, 16) == 1.0f);
    CHECK(stickzoom_factor(99, 16) > 1.0f);
    CHECK(stickzoom_factor(157, 16) < 1.0f);
    float z = 1;
    for (int i=0;i<50;i++) z *= stickzoom_factor(0,20);
    CHECK(fabsf(z-2) < .0001f);
    for (int i=0;i<50;i++) z *= stickzoom_factor(255,20);
    CHECK(fabsf(z-1) < .0001f);
    CHECK(stickzoom_factor(0,1000) == stickzoom_factor(0,50));
    fit_view_t v; fit_view_init(&v,960,544);
    for (int i=0;i<1000;i++) fit_set_zoom_at(&v,800,1200,v.zoom*stickzoom_factor(0,16),480,272);
    CHECK(v.zoom == FIT_ZOOM_MAX);
    fit_pan_by(&v,800,1200,-100,-100);
    CHECK(v.pan_x < 0 && v.pan_y < 0);
    for (int i=0;i<1000;i++) fit_set_zoom_at(&v,800,1200,v.zoom*stickzoom_factor(255,16),480,272);
    CHECK(v.zoom == FIT_ZOOM_MIN);
    CHECK(v.pan_x == 0 && v.pan_y == 0);
    return TEST_DONE("stickzoom");
}
