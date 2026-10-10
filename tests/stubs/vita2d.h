#ifndef TEST_VITA2D_H
#define TEST_VITA2D_H
typedef struct vita2d_texture vita2d_texture;
#define RGBA8(r,g,b,a) ((unsigned)(r) | ((unsigned)(g)<<8) | ((unsigned)(b)<<16) | ((unsigned)(a)<<24))
unsigned int vita2d_texture_get_width(const vita2d_texture *t);
unsigned int vita2d_texture_get_height(const vita2d_texture *t);
void vita2d_draw_texture_part_scale(const vita2d_texture *t,float x,float y,float tx,float ty,float tw,float th,float sx,float sy);
void vita2d_draw_fill_circle(float x,float y,float r,unsigned int c);
void vita2d_draw_line(float x1,float y1,float x2,float y2,unsigned int c);
int vita2d_get_clipping_enabled(void);
void vita2d_get_clip_rectangle(int *x1,int *y1,int *x2,int *y2);
void vita2d_set_clip_rectangle(int x1,int y1,int x2,int y2);
void vita2d_enable_clipping(void);
void vita2d_disable_clipping(void);
#endif
