/* Real grid/menu input and menu layout, with only the renderer replaced. */
#include "test.h"
#include "ui/grid.h"
#include "ui/menu.h"
#include "ui/font.h"
#include "ui/text.h"
#include <string.h>

static int bad_bounds;
void draw_rect(int x,int y,int w,int h,unsigned c)
{ (void)c; if (x<0||y<0||x+w>960||y+h>521) bad_bounds++; }
void draw_rrect(int x,int y,int w,int h,int r,unsigned c)
{ (void)r; draw_rect(x,y,w,h,c); }
void draw_selector(int x,int y,int w,int h,uint64_t now)
{ (void)now; draw_rect(x,y,w,h,0); }
void font_draw(int x,int y,unsigned c,int px,const char *s)
{ (void)c;(void)s; if (x<0||y<0||y+px>521) bad_bounds++; }
int font_height(int px) { return px; }
void text_ellipsize(int px,const char *s,int w,char out[TEXT_LINE_MAX])
{ (void)px;(void)w; snprintf(out,TEXT_LINE_MAX,"%s",s); }

static int grid_tap(grid_t *g,int x,int y)
{
	input_t in={0};in.touch_began=1;in.began_x=x;in.began_y=y;
	in.touch_count=1;in.touch[0].x=x;in.touch[0].y=y;
	CHECK_INT(grid_update(g,&in),GRID_NONE);
	memset(&in,0,sizeof in);return grid_update(g,&in);
}
static int menu_tap(int x,int y)
{
	input_t in={0};in.touch_began=1;in.began_x=x;in.began_y=y;
	in.touch_count=1;in.touch[0].x=x;in.touch[0].y=y;
	CHECK_INT(menu_update(&in),-1);
	memset(&in,0,sizeof in);return menu_update(&in);
}

int main(void)
{
	grid_t grid;grid_init(&grid);grid_set_count(&grid,12);grid.actions=1;
	CHECK_INT(grid_tap(&grid,370,346),GRID_MENU);CHECK_INT(grid.sel,0);
	CHECK_INT(grid_tap(&grid,552,346),GRID_MENU);CHECK_INT(grid.sel,1);
	CHECK_INT(grid_tap(&grid,265,200),GRID_OPEN);CHECK_INT(grid.sel,0);
	grid.actions=0;CHECK_INT(grid_tap(&grid,370,346),GRID_OPEN);
	grid.actions=1;
	input_t in={0};in.touch_began=1;in.began_x=370;in.began_y=346;
	in.touch_count=1;in.touch[0].x=370;in.touch[0].y=346;
	CHECK_INT(grid_update(&grid,&in),GRID_NONE);
	in.touch_began=0;in.touch[0].y=260;
	CHECK_INT(grid_update(&grid,&in),GRID_NONE);
	memset(&in,0,sizeof in);CHECK_INT(grid_update(&grid,&in),GRID_NONE);
	/* Scroll changes the menu's hit target together with its visible geometry. */
	grid.scroll=200;grid.vel=0;
	CHECK_INT(grid_tap(&grid,370,450),GRID_MENU);CHECK_INT(grid.sel,4);
	const char *labels[12]={"0","1","2","3","4","5","6","7","8","9","10","11"};
	menu_open("Long reader menu",labels,12);menu_draw(0);CHECK_INT(bad_bounds,0);
	menu_select(11);menu_draw(0);CHECK_INT(bad_bounds,0);
	in.pressed=IN_CONFIRM;CHECK_INT(menu_update(&in),11);CHECK(!menu_is_open());
	menu_open("Volume menu",labels,11);
	/* 11 entries: top=24, header=48, 10 visible rows then a 24px footer. */
	CHECK_INT(menu_tap(450,480),-1);CHECK(menu_is_open());
	CHECK_INT(menu_tap(450,452),10);CHECK(!menu_is_open());
	menu_open("Long reader menu",labels,12);
	in=(input_t){0};in.touch_began=1;in.began_x=450;in.began_y=400;
	in.touch_count=1;in.touch[0].x=450;in.touch[0].y=400;
	CHECK_INT(menu_update(&in),-1);
	in.touch_began=0;in.touch[0].y=310;CHECK_INT(menu_update(&in),-1);
	in.touch_count=0;CHECK_INT(menu_update(&in),-1);CHECK(menu_is_open());
	CHECK_INT(menu_tap(450,452),11);
	menu_open("Short menu",labels,2);
	CHECK_INT(menu_tap(450,300),1);
	/* A horizontal gesture over a destructive/action target is never a tap. */
	grid_init(&grid);grid_set_count(&grid,12);grid.actions=1;
	in=(input_t){0};in.touch_began=1;in.began_x=370;in.began_y=346;
	in.touch_count=1;in.touch[0].x=370;in.touch[0].y=346;
	CHECK_INT(grid_update(&grid,&in),GRID_NONE);
	in.touch_began=0;in.touch[0].x=300;CHECK_INT(grid_update(&grid,&in),GRID_NONE);
	in.touch_count=0;CHECK_INT(grid_update(&grid,&in),GRID_NONE);
	menu_open("Short menu",labels,2);
	in=(input_t){0};in.touch_began=1;in.began_x=450;in.began_y=300;
	in.touch_count=1;in.touch[0].x=450;in.touch[0].y=300;
	CHECK_INT(menu_update(&in),-1);
	in.touch_began=0;in.touch[0].x=550;CHECK_INT(menu_update(&in),-1);
	in.touch_count=0;CHECK_INT(menu_update(&in),-1);CHECK(menu_is_open());
	in.pressed=IN_BACK;CHECK_INT(menu_update(&in),-1);CHECK(!menu_is_open());
	return TEST_DONE("shelf_ui");
}
