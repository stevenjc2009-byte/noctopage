#include "test.h"
#include "lib/reading_clock.h"

int main(void)
{
	reading_clock_t c={0};
	CHECK_U64(reading_clock_tick(&c,1000,0,0,0),0);
	CHECK_U64(reading_clock_tick(&c,2000,0,0,0),0); /* loading */
	CHECK_U64(reading_clock_tick(&c,3000,0,1,1),0);
	CHECK_INT(c.pages,1);
	CHECK_U64(reading_clock_tick(&c,4000,0,1,0),1000);
	CHECK_INT(c.pages,1);
	reading_clock_tick(&c,5000,1,1,1); CHECK_INT(c.pages,2);
	reading_clock_tick(&c,6000,0,1,1); CHECK_INT(c.pages,3); /* revisits are views */
	CHECK_U64(reading_clock_tick(&c,7000,0,0,1),0); /* menus */
	CHECK_U64(reading_clock_tick(&c,500000,0,1,0),0); /* suspend */
	CHECK_U64(reading_clock_tick(&c,501000,0,1,0),0); /* idle */
	CHECK_U64(reading_clock_tick(&c,502000,0,1,1),1000);
	CHECK_U64(reading_clock_tick(&c,500000,0,1,0),0); /* clock rolled back */
	return TEST_DONE("reading_clock");
}
