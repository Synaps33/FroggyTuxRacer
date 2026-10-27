#ifndef TR_GFX_H
#define TR_GFX_H

#include "tr.h"
#include "tr_asset.h"

#define TR_NEAR_Q8 64
#define TR_FAR_Q8 (115 * 256)

/* camera */
void gfx_set_camera(v3 eye, v3 target);
void gfx_project(v3 world, int *sx, int *sy, int *visible);

/* frame */
void gfx_frame_begin(void);
void gfx_clear_depth(void);
void gfx_sky(void);
uint16_t *gfx_fb(void);

/* scene */
void gfx_draw_terrain(tr_course *c, fx player_x, fx player_z);
void gfx_draw_world_objects(tr_course *c, fx player_x, fx player_z, fx time_s, int show_gates, const uint8_t *item_taken);
void gfx_draw_tux_model(v3 pos, ang yaw, ang pitch, ang roll, fx speed,
                        int turn, int braking, int paddling, int airborne);
void gfx_draw_tux_shadow(v3 pos);

/* 2D UI */
void gfx_rect(int x, int y, int w, int h, uint16_t color, int alpha);
void gfx_hline(int x, int y, int w, uint16_t color, int alpha);
void gfx_blit(tr_tex *t, int x, int y, int w, int h, uint16_t tint, int alpha, int bilinear);
void gfx_blit_sub(tr_tex *t, int sx, int sy, int sw, int sh,
                  int dx, int dy, int dw, int dh, uint16_t tint, int alpha, int bilinear);
int gfx_text_width(tr_font *f, const char *s, int size);
void gfx_text(tr_font *f, tr_tex *t, const char *s, int x, int y, int size,
              uint16_t tint, int align);
void gfx_snowflake(int x, int y, int size, int alpha);

/* colours */
#define TR_RGB565(r,g,b) ((uint16_t)((((r) & 0xf8) << 8) | (((g) & 0xfc) << 3) | ((b) >> 3)))
#define TR_WHITE 0xFFFF
#define TR_BLACK 0x0000
#define TR_YELLOW 0xFFE0
#define TR_ORANGE 0xFBE0
#define TR_UI_DARK 0x10A6
#define TR_UI_BLUE 0x2149
#define TR_FOG_COLOR 0xCED9

#endif
