#include "ui/input.h"

#include <psp2/apputil.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/system_param.h>
#include <psp2/touch.h>
#include <string.h>

#define REPEAT_DELAY_MS 400
#define REPEAT_RATE_MS  80
#define STICK_DEADZONE  60 /* distance from centre 128 that counts as a d-pad press */


static int g_confirm_cross = 1;
static uint32_t g_inject;
static uint32_t g_prev_held;
static uint64_t g_repeat_at[4];
static int g_prev_touch_ids[INPUT_MAX_TOUCH];
static int g_prev_touch_n;

void input_init(void)
{
	SceAppUtilInitParam init;
	SceAppUtilBootParam boot;
	memset(&init, 0, sizeof init);
	memset(&boot, 0, sizeof boot);
	sceAppUtilInit(&init, &boot); /* fails harmlessly if already initialised */

	int v = 0;
	if (sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_ENTER_BUTTON, &v) >= 0)
		g_confirm_cross = (v == SCE_SYSTEM_PARAM_ENTER_BUTTON_CROSS);

	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
	sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
}

int input_confirm_is_cross(void)
{
	return g_confirm_cross;
}

void input_inject_set(uint32_t held_mask)
{
	g_inject = held_mask;
}

static uint32_t read_buttons(input_t *in)
{
	SceCtrlData pad;
	memset(&pad, 0, sizeof pad);
	sceCtrlPeekBufferPositive(0, &pad, 1);

	uint32_t m = 0;
	if (pad.buttons & SCE_CTRL_UP)       m |= IN_UP;
	if (pad.buttons & SCE_CTRL_DOWN)     m |= IN_DOWN;
	if (pad.buttons & SCE_CTRL_LEFT)     m |= IN_LEFT;
	if (pad.buttons & SCE_CTRL_RIGHT)    m |= IN_RIGHT;
	if (pad.buttons & SCE_CTRL_CROSS)    m |= IN_CROSS;
	if (pad.buttons & SCE_CTRL_CIRCLE)   m |= IN_CIRCLE;
	if (pad.buttons & SCE_CTRL_SQUARE)   m |= IN_SQUARE;
	if (pad.buttons & SCE_CTRL_TRIANGLE) m |= IN_TRIANGLE;
	if (pad.buttons & (SCE_CTRL_LTRIGGER | SCE_CTRL_L1)) m |= IN_L;
	if (pad.buttons & (SCE_CTRL_RTRIGGER | SCE_CTRL_R1)) m |= IN_R;
	if (pad.buttons & SCE_CTRL_START)    m |= IN_START;
	if (pad.buttons & SCE_CTRL_SELECT)   m |= IN_SELECT;

	in->lx = pad.lx; in->ly = pad.ly;
	in->rx = pad.rx; in->ry = pad.ry;

	/* Left stick acts as the d-pad. */
	if (pad.lx < 128 - STICK_DEADZONE) m |= IN_LEFT;
	if (pad.lx > 128 + STICK_DEADZONE) m |= IN_RIGHT;
	if (pad.ly < 128 - STICK_DEADZONE) m |= IN_UP;
	if (pad.ly > 128 + STICK_DEADZONE) m |= IN_DOWN;
	return m;
}

static void read_touch(input_t *in)
{
	SceTouchData td;
	memset(&td, 0, sizeof td);
	sceTouchPeek(SCE_TOUCH_PORT_FRONT, &td, 1);

	int n = (int)td.reportNum;
	if (n > INPUT_MAX_TOUCH)
		n = INPUT_MAX_TOUCH;
	in->touch_count = n;
	in->touch_began = 0;
	for (int i = 0; i < n; i++) {
		in->touch[i].id = td.report[i].id;
		/* Panel is 1920x1088; the screen is 960x544. */
		in->touch[i].x = td.report[i].x / 2;
		in->touch[i].y = td.report[i].y / 2;

		int known = 0;
		for (int j = 0; j < g_prev_touch_n; j++)
			if (g_prev_touch_ids[j] == in->touch[i].id)
				known = 1;
		if (!known && !in->touch_began) {
			in->touch_began = 1;
			in->began_x = in->touch[i].x;
			in->began_y = in->touch[i].y;
		}
	}
	for (int i = 0; i < n; i++)
		g_prev_touch_ids[i] = in->touch[i].id;
	g_prev_touch_n = n;
}

void input_poll(input_t *in)
{
	memset(in, 0, sizeof *in);
	in->now_ms = sceKernelGetProcessTimeWide() / 1000;

	uint32_t held = read_buttons(in) | g_inject;

	/* Abstract confirm/back from the physical button the system calls "enter". */
	uint32_t enter = g_confirm_cross ? IN_CROSS : IN_CIRCLE;
	uint32_t back  = g_confirm_cross ? IN_CIRCLE : IN_CROSS;
	if (held & enter) held |= IN_CONFIRM;
	if (held & back)  held |= IN_BACK;

	uint32_t pressed  = held & ~g_prev_held;
	uint32_t released = g_prev_held & ~held;

	/* D-pad key repeat: first repeat after 400 ms, then every 80 ms. */
	for (int i = 0; i < 4; i++) {
		uint32_t bit = 1u << i;
		if (!(held & bit))
			continue;
		if (pressed & bit) {
			g_repeat_at[i] = in->now_ms + REPEAT_DELAY_MS;
		} else if (in->now_ms >= g_repeat_at[i]) {
			pressed |= bit;
			g_repeat_at[i] = in->now_ms + REPEAT_RATE_MS;
		}
	}

	in->held = held;
	in->pressed = pressed;
	in->released = released;
	g_prev_held = held;

	read_touch(in);
}
