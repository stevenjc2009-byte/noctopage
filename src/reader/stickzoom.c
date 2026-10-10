#include "reader/stickzoom.h"
#include <math.h>
#include <stdlib.h>
float stickzoom_factor(int axis, unsigned milliseconds)
{
    if (axis < 0) axis = 0;
    if (axis > 255) axis = 255;
    int d = axis - 128;
    if (abs(d) <= 28) return 1.0f;
    float tilt = d < 0 ? (float)(d + 28) / 100.0f : (float)(d - 28) / 99.0f;
    if (milliseconds > 50) milliseconds = 50;
    return exp2f(-tilt * (float)milliseconds / 1000.0f);
}
