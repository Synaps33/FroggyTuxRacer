#ifndef TR_ASSET_H
#define TR_ASSET_H

#include "tr.h"

#define TR_NUM_COURSES 5

typedef struct {
    int w, h;
    uint8_t fmt;           /* 0 = RGB565, 1 = RGB565 + A8 */
    uint16_t *px;
    uint8_t *a;
} tr_tex;

typedef struct {
    uint8_t w, h;
    int8_t xo, yo, kern;
    int16_t xp, yp;
} tr_glyph;

typedef struct {
    int texw, texh, max_ascent, max_descent, src_scale;
    tr_glyph g[256];
} tr_font;

typedef struct {
    fx x, z;
    fx h, d, above;
    uint8_t type;
    uint8_t flags;
} tr_obj;

typedef struct {
    char id[24];
    char name[40];
    char desc[200];
    int par_tenths;
} tr_course_info;

typedef struct {
    int nx, ny;
    fx width, length, play_width, play_length;
    fx start_x, start_z, elev_scale;
    fx angle_rad;
    fx finish_z, start_item_z;
    fx *heights;          /* Q16.16 metres, row-major */
    uint8_t *surfaces;    /* 0 ice, 1 rock, 2 snow */
    tr_obj *trees;
    int num_trees;
    tr_obj *items;
    int num_items;
} tr_course;

/* global textures (loaded once) */
extern tr_tex *tr_tex_snow, *tr_tex_rock, *tr_tex_ice;
extern tr_tex *tr_tex_tree[3];
extern tr_tex *tr_tex_item[5];
extern tr_tex *tr_tex_font_white, *tr_tex_font_yellow;
extern tr_tex *tr_tex_logo, *tr_tex_icon, *tr_tex_snowp;
extern tr_tex *tr_tex_preview[TR_NUM_COURSES];
extern tr_font tr_font_white, tr_font_yellow;
extern tr_course_info tr_courses[TR_NUM_COURSES];

int tr_assets_init(const char *content_path);
void tr_assets_shutdown(void);
int tr_course_load(int idx);
void tr_course_unload(void);
tr_course *tr_current_course(void);
const char *tr_asset_root(void);

/* height query, world Q16; returns Q16 height. Also selects surface 0..2. */
fx tr_course_height(fx x, fx z);
int tr_course_surface(fx x, fx z);
v3 tr_course_normal(fx x, fx z);

#endif
