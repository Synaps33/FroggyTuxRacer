#include "tr_game.h"
#include "tr_asset.h"
#include "tr_gfx.h"
#include "tr_audio.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BTN_UP     (1u << 0)
#define BTN_DOWN   (1u << 1)
#define BTN_LEFT   (1u << 2)
#define BTN_RIGHT  (1u << 3)
#define BTN_A      (1u << 4)
#define BTN_B      (1u << 5)
#define BTN_X      (1u << 6)
#define BTN_Y      (1u << 7)
#define BTN_L      (1u << 8)
#define BTN_R      (1u << 9)
#define BTN_START  (1u << 10)
#define BTN_SELECT (1u << 11)

#define GRAV 642252        /* 9.8 m/s^2 Q16 */
#define RAD_TO_ANG 10430   /* 65536 / (2*pi) */

typedef enum {
    MODE_TITLE = 0,
    MODE_COURSE,
    MODE_RACE,
    MODE_PAUSE,
    MODE_RESULTS,
    MODE_CONTROLS,
    MODE_CREDITS
} game_mode;

typedef struct {
    game_mode mode;
    int title_sel, course_sel, pause_sel, result_sel;
    int menu_lock;
    int free_camera;
    int course_idx;
    fx race_time;
    fx countdown;
    fx posx, posy, posz;
    fx speed, vy;
    ang heading;
    int grounded;
    int airborne;
    fx pitch, roll;
    v3 cam, target;
    int cam_init;
    fx crash_timer;
    int herring, herring_total;
    uint8_t item_taken[512];
    fx respawn_x, respawn_z;
    fx finish_z;
    fx best_time[TR_NUM_COURSES];
    int best_herring[TR_NUM_COURSES];
    fx fade;
    int finish_new_best;
    fx finish_time;
    int finish_herring;
    uint32_t frame;
    unsigned prev_buttons;
    int quit;
    int snow_init;
    int snow_x[48], snow_y[48], snow_v[48], snow_sz[48];
} game_state;

static game_state g;

#ifndef TR_TEST_HOOKS
static void load_scores(void)
{
    char path[700];
    snprintf(path, sizeof(path), "%s/tuxracer.sav", tr_asset_root());
    FILE *f = fopen(path, "rb");
    if (!f) return;
    char magic[4];
    int32_t v[TR_NUM_COURSES];
    int32_t h[TR_NUM_COURSES];
    if (fread(magic, 1, 4, f) == 4 && memcmp(magic, "TSAV", 4) == 0 &&
        fread(v, 4, TR_NUM_COURSES, f) == TR_NUM_COURSES &&
        fread(h, 4, TR_NUM_COURSES, f) == TR_NUM_COURSES) {
        for (int i = 0; i < TR_NUM_COURSES; i++) {
            g.best_time[i] = (fx)(((int64_t)v[i] * FX_ONE) / 100);
            g.best_herring[i] = h[i];
        }
    }
    fclose(f);
}
static void save_scores(void)
{
    char path[700];
    snprintf(path, sizeof(path), "%s/tuxracer.sav", tr_asset_root());
    FILE *f = fopen(path, "wb");
    if (!f) return;
    int32_t v[TR_NUM_COURSES], h[TR_NUM_COURSES];
    for (int i = 0; i < TR_NUM_COURSES; i++) {
        v[i] = (int32_t)(((int64_t)g.best_time[i] * 100) / FX_ONE);
        h[i] = g.best_herring[i];
    }
    fwrite("TSAV", 1, 4, f);
    fwrite(v, 4, TR_NUM_COURSES, f);
    fwrite(h, 4, TR_NUM_COURSES, f);
    fclose(f);
}
#endif

void tr_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
#if defined(SF2000) || defined(GB300V2)
    FILE *f = fopen("/mnt/sda1/tuxracer.log", "a");
    if (f) {
        vfprintf(f, fmt, ap);
        fputc('\n', f);
        fclose(f);
    }
#else
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
#endif
    va_end(ap);
}

uint32_t tr_ticks_ms(void)
{
#if defined(SF2000) || defined(GB300V2)
    extern uint32_t os_get_tick_count(void) __attribute__((weak));
    if (os_get_tick_count) return os_get_tick_count();
#endif
    return 0;
}

static unsigned input_mask(const tr_input *in)
{
    unsigned m = 0;
    if (!in) return 0;
    if (in->up) m |= BTN_UP;
    if (in->down) m |= BTN_DOWN;
    if (in->left) m |= BTN_LEFT;
    if (in->right) m |= BTN_RIGHT;
    if (in->a) m |= BTN_A;
    if (in->b) m |= BTN_B;
    if (in->x) m |= BTN_X;
    if (in->y) m |= BTN_Y;
    if (in->l) m |= BTN_L;
    if (in->r) m |= BTN_R;
    if (in->start) m |= BTN_START;
    if (in->select) m |= BTN_SELECT;
    return m;
}

static fx mheight(fx x, fx z) { return tr_course_height(x, z); }

static void snow_init(void)
{
    if (g.snow_init) return;
    for (int i = 0; i < 48; i++) {
        g.snow_x[i] = (int)((i * 67 + 31) % TR_W);
        g.snow_y[i] = (int)((i * 97 + 17) % TR_H);
        g.snow_v[i] = 1 + (i % 3);
        g.snow_sz[i] = (i % 5 == 0) ? 3 : 2;
    }
    g.snow_init = 1;
}

static void set_camera_now(void)
{
    fx fwdx = tr_sin(g.heading), fwdz = -tr_cos(g.heading);
    fx px = g.posx, pz = g.posz;
    fx py = g.posy;
    v3 desired = v3_make(px - fx_mul(fwdx, FX(5.2)),
                         py + FX(3.1),
                         pz - fx_mul(fwdz, FX(5.2)));
    fx h = mheight(desired.x, desired.z);
    if (desired.y < h + FX(0.9)) desired.y = h + FX(0.9);
    g.cam = desired;
    fx tx = px + fx_mul(fwdx, FX(10.0));
    fx tz = pz + fx_mul(fwdz, FX(10.0));
    g.target = v3_make(tx, mheight(tx, tz) + FX(1.2), tz);
    g.cam_init = 1;
#ifdef TR_DEBUG_GFX
    tr_log("dbg setcam pos=(%d,%d,%d) cam=(%d,%d,%d) target=(%d,%d,%d) mh=%d",
           g.posx>>16, g.posy>>16, g.posz>>16,
           g.cam.x>>16, g.cam.y>>16, g.cam.z>>16,
           g.target.x>>16, g.target.y>>16, g.target.z>>16, mheight(tx,tz)>>16);
#endif
}

static void reset_player(int course_idx)
{
    tr_course *c = tr_current_course();
    (void)course_idx;
    g.posx = c->start_x;
    g.posz = c->start_z;
    g.posy = mheight(g.posx, g.posz) + FX(0.02);
    g.speed = 0;
    g.vy = 0;
    g.heading = 0;
    g.grounded = 1;
    g.airborne = 0;
    g.pitch = 0;
    g.roll = 0;
    g.crash_timer = 0;
    g.race_time = 0;
    g.countdown = 3 * FX_ONE;
    g.herring = 0;
    g.respawn_x = g.posx;
    g.respawn_z = c->start_z;
    g.finish_z = c->finish_z;
    g.cam_init = 0;
    memset(g.item_taken, 0, sizeof(g.item_taken));
    g.herring_total = 0;
    for (int i = 0; i < c->num_items; i++) {
        if (c->items[i].type == 0) g.herring_total++;
    }
    set_camera_now();
}

static void scene_load(int idx)
{
    if (!tr_course_load(idx)) return;
    g.course_idx = idx;
    tr_course *c = tr_current_course();
    g.posx = c->start_x;
    g.posz = c->start_z;
    g.posy = mheight(g.posx, g.posz);
    g.heading = 0;
    g.speed = 0;
    g.vy = 0;
    g.grounded = 1;
    g.airborne = 0;
    g.cam_init = 0;
    set_camera_now();
}

static void start_race(void)
{
    g.course_idx = g.course_sel;
    if (!tr_course_load(g.course_idx)) {
        tr_log("Tux Racer: cannot load selected course %d", g.course_idx);
        return;
    }
    reset_player(g.course_idx);
    g.mode = MODE_RACE;
    g.pause_sel = 0;
    g.result_sel = 0;
    g.finish_new_best = 0;
    tr_audio_play_race_start();
}

static void finish_race(void)
{
    if (g.mode != MODE_RACE) return;
    g.finish_time = g.race_time;
    g.finish_herring = g.herring;
    g.finish_new_best = 0;
    if (g.best_time[g.course_idx] == 0 || g.race_time < g.best_time[g.course_idx]) {
        g.best_time[g.course_idx] = g.race_time;
        g.best_herring[g.course_idx] = g.herring;
        g.finish_new_best = 1;
    }
#ifndef TR_TEST_HOOKS
    save_scores();
#endif
    g.mode = MODE_RESULTS;
    g.result_sel = 0;
}

static void update_camera(fx dt)
{
    if (g.free_camera) { gfx_set_camera(g.cam, g.target); return; }
    fx fwdx = tr_sin(g.heading), fwdz = -tr_cos(g.heading);
    fx py = g.posy;
    v3 desired = v3_make(g.posx - fx_mul(fwdx, FX(5.2)),
                         py + FX(3.1),
                         g.posz - fx_mul(fwdz, FX(5.2)));
    fx h = mheight(desired.x, desired.z);
    if (desired.y < h + FX(0.9)) desired.y = h + FX(0.9);
    fx tx = g.posx + fx_mul(fwdx, FX(10.0));
    fx tz = g.posz + fx_mul(fwdz, FX(10.0));
    v3 target = v3_make(tx, mheight(tx, tz) + FX(1.2), tz);
    if (!g.cam_init) {
        g.cam = desired;
        g.target = target;
        g.cam_init = 1;
    } else {
        fx k = FX(0.18);
        if (dt > 0) {
            fx kk = fx_mul(k, dt * 60);
            if (kk > FX(0.5)) kk = FX(0.5);
            k = kk;
        }
        g.cam.x += fx_mul(desired.x - g.cam.x, k);
        g.cam.y += fx_mul(desired.y - g.cam.y, k);
        g.cam.z += fx_mul(desired.z - g.cam.z, k);
        g.target.x += fx_mul(target.x - g.target.x, k);
        g.target.y += fx_mul(target.y - g.target.y, k);
        g.target.z += fx_mul(target.z - g.target.z, k);
    }
#ifdef TR_DEBUG_GFX
    if ((g.frame % 120) == 0) {
        tr_log("dbg cam pos=(%d,%d,%d) cam=(%d,%d,%d) target=(%d,%d,%d)",
               g.posx>>16, g.posy>>16, g.posz>>16,
               g.cam.x>>16, g.cam.y>>16, g.cam.z>>16,
               g.target.x>>16, g.target.y>>16, g.target.z>>16);
    }
#endif
    gfx_set_camera(g.cam, g.target);
}

static void update_physics(fx dt)
{
    tr_course *c = tr_current_course();
    if (!c) return;

    fx fwdx = tr_sin(g.heading), fwdz = -tr_cos(g.heading);
    fx h0 = mheight(g.posx, g.posz);
    fx h1 = mheight(g.posx + fwdx, g.posz + fwdz);
    fx slope = h1 - h0;
    fx acc = -fx_mul(GRAV, slope);
    g.speed += fx_mul(acc, dt);

    int surf = tr_course_surface(g.posx, g.posz);
    fx drag = surf == 0 ? FX(0.055) : (surf == 1 ? FX(0.55) : FX(0.20));
    g.speed -= fx_mul(fx_mul(g.speed, drag), dt);

    if (g.grounded) {
        unsigned btns = 0;
        /* caller stores held state in prev_buttons as well; paddle uses the
           globally sampled state, copied here by tr_game_update */
        btns = g.prev_buttons;
        if (btns & BTN_UP) g.speed += fx_mul(FX(3.2), dt);
        if (btns & BTN_DOWN) {
            g.speed -= fx_mul(FX(10.0), dt);
            if (g.speed < 0) g.speed = 0;
        }
        fx spd = g.speed;
        fx factor = FX(0.35) + fx_div(spd, FX(14));
        if (factor < FX(0.35)) factor = FX(0.35);
        if (factor > FX(1.15)) factor = FX(1.15);
        fx rad = fx_mul(fx_mul(FX(1.5), dt), factor);
        int d = (int)((rad * RAD_TO_ANG) >> 16);
        if (btns & BTN_LEFT) g.heading = (ang)(g.heading - d);
        if (btns & BTN_RIGHT) g.heading = (ang)(g.heading + d);
    } else {
        fx rad = fx_mul(FX(0.35), dt);
        int d = (int)((rad * RAD_TO_ANG) >> 16);
        if (g.prev_buttons & BTN_LEFT) g.heading = (ang)(g.heading - d);
        if (g.prev_buttons & BTN_RIGHT) g.heading = (ang)(g.heading + d);
    }

    if (g.speed < 0) g.speed = 0;
    if (g.speed > FX(34)) g.speed = FX(34);

    fwdx = tr_sin(g.heading);
    fwdz = -tr_cos(g.heading);
    fx step = fx_mul(g.speed, dt);
    fx nx = g.posx + fx_mul(fwdx, step);
    fx nz = g.posz + fx_mul(fwdz, step);
    fx boundary = fx_div(c->width - c->play_width, 2 * FX_ONE);
    fx minx = boundary + FX(0.3);
    fx maxx = c->width - boundary - FX(0.3);
    if (nx < minx) { nx = minx; g.speed -= fx_mul(g.speed, FX(0.5)); }
    if (nx > maxx) { nx = maxx; g.speed -= fx_mul(g.speed, FX(0.5)); }
    if (nz > 0) nz = 0;
    if (g.speed < 0) g.speed = 0;

    fx nh = mheight(nx, nz);
    if (g.grounded) {
        fx drop = g.posy - nh;
        if (drop > FX(0.8) && g.speed > FX(8)) {
            g.grounded = 0;
            g.vy = 0;
        } else {
            g.posy = nh;
        }
    }
    if (g.grounded) {
        g.posx = nx;
        g.posz = nz;
        g.posy = mheight(nx, nz);
    } else {
        g.vy -= fx_mul(GRAV, dt);
        g.posy += fx_mul(g.vy, dt);
        g.posx = nx;
        g.posz = nz;
        fx ground = mheight(g.posx, g.posz);
        if (g.posy <= ground) {
            g.posy = ground;
            g.vy = 0;
            g.grounded = 1;
            g.speed -= fx_mul(g.speed, FX(0.05));
        }
    }

    /* herring collection */
    for (int i = 0; i < c->num_items; i++) {
        if (c->items[i].type != 0 || g.item_taken[i & 511]) continue;
        fx dx = g.posx - c->items[i].x;
        fx dz = g.posz - c->items[i].z;
        if (dx > -FX(1.4) && dx < FX(1.4) && dz > -FX(1.4) && dz < FX(1.4)) {
            g.item_taken[i & 511] = 1;
            g.herring++;
            tr_audio_play_herring();
        }
    }

    /* reset points */
    for (int i = 0; i < c->num_items; i++) {
        if (c->items[i].type == 4 && g.posz <= c->items[i].z && c->items[i].z < g.respawn_z) {
            g.respawn_x = c->items[i].x;
            g.respawn_z = c->items[i].z;
        }
    }

    /* tree collisions (only near objects) */
    if (g.grounded && g.crash_timer <= 0) {
        for (int i = 0; i < c->num_trees; i++) {
            tr_obj *o = &c->trees[i];
            fx dz = o->z - g.posz;
            if (dz > FX(5) || dz < -FX(8)) continue;
            fx dx = o->x - g.posx;
            if (dx > -FX(0.8) && dx < FX(0.8) && dz > -FX(0.8) && dz < FX(0.8)) {
                g.crash_timer = FX(0.55);
                g.speed = fx_mul(g.speed, FX(0.18));
                g.posx -= fx_mul(dx, FX(0.15));
                g.posz -= fx_mul(dz, FX(0.15));
                tr_audio_play_hit();
                break;
            }
        }
    }

    if (g.crash_timer > 0) g.crash_timer -= dt;
    g.race_time += dt;

    /* camera and pose */
    fx spd_ratio = g.speed > FX(22) ? FX(1) : fx_div(g.speed, FX(22));
    fx fh = mheight(g.posx + fwdx, g.posz + fwdz);
    fx bh = mheight(g.posx - fwdx, g.posz - fwdz);
    fx fslope = fx_div(fh - bh, FX(2));
    fx rgtx = tr_cos(g.heading), rgtz = tr_sin(g.heading);
    fx rh = mheight(g.posx + rgtx, g.posz + rgtz);
    fx lh = mheight(g.posx - rgtx, g.posz - rgtz);
    fx rslope = fx_div(rh - lh, FX(2));
    ang tpitch = (ang)(int)(((int64_t)fslope * RAD_TO_ANG) >> 16);
    ang troll = (ang)(int)(((int64_t)rslope * RAD_TO_ANG) >> 16);
    if (!g.grounded) {
        int vy_ang = (int)(((int64_t)g.vy * RAD_TO_ANG) >> 16);
        tpitch = (ang)(vy_ang / 4);
        if (g.vy < 0) tpitch = (ang)(-ANG_90 / 8);
    }
    int steer_roll = 0;
    if (g.prev_buttons & BTN_LEFT) steer_roll = -ANG_90 / 7;
    if (g.prev_buttons & BTN_RIGHT) steer_roll = ANG_90 / 7;
    troll = (ang)(troll + (int)(((int64_t)steer_roll * spd_ratio) >> 16));
    g.pitch = (fx)((int)g.pitch + (((int)tpitch - (int)g.pitch) / 5));
    g.roll = (fx)((int)g.roll + (((int)troll - (int)g.roll) / 4));
    g.airborne = !g.grounded;

    if (g.posz <= g.finish_z + FX(0.5)) finish_race();
    else if (g.posz <= -c->play_length) finish_race();
}

static void update_snow(void)
{
    snow_init();
    for (int i = 0; i < 48; i++) {
        g.snow_y[i] += g.snow_v[i];
        g.snow_x[i] += (int)(tr_sin((ang)(g.frame * 7 + i * 300)) >> 12);
        if (g.snow_y[i] >= TR_H) {
            g.snow_y[i] = -g.snow_sz[i];
            g.snow_x[i] = (int)((g.frame * 37 + i * 53) % TR_W);
        }
        if (g.snow_x[i] < 0) g.snow_x[i] += TR_W;
        if (g.snow_x[i] >= TR_W) g.snow_x[i] -= TR_W;
    }
}

void tr_game_init(void)
{
    memset(&g, 0, sizeof(g));
    g.mode = MODE_TITLE;
    g.title_sel = 0;
    g.course_sel = 0;
    g.course_idx = 0;
    snow_init();
#ifndef TR_TEST_HOOKS
    load_scores();
#endif
    tr_audio_init();
    if (tr_course_load(0)) scene_load(0);
    gfx_frame_begin();
}

void tr_game_shutdown(void)
{
    tr_audio_shutdown();
    tr_assets_shutdown();
}

int tr_game_wants_quit(void) { return g.quit; }

static void menu_move(int *sel, int n, unsigned pressed)
{
    if (pressed & BTN_UP) *sel = (*sel + n - 1) % n;
    if (pressed & BTN_DOWN) *sel = (*sel + 1) % n;
}

void tr_game_update(const tr_input *in, uint32_t dt_ms)
{
    unsigned cur = input_mask(in);
    unsigned pressed = cur & ~g.prev_buttons;
    g.prev_buttons = cur;
    if (dt_ms > 60) dt_ms = 60;
    fx dt = (fx)(((int64_t)dt_ms * FX_ONE) / 1000);
    if (dt <= 0) dt = FX_ONE / 60;
    g.frame++;
    update_snow();

    if (g.mode == MODE_TITLE) {
        menu_move(&g.title_sel, 3, pressed);
        if (pressed & BTN_A) {
            if (g.title_sel == 0) {
                g.mode = MODE_COURSE;
                g.course_sel = g.course_idx;
                g.menu_lock = 10;
            } else if (g.title_sel == 1) {
                g.mode = MODE_CONTROLS;
            } else {
                g.mode = MODE_CREDITS;
            }
        }
        update_camera(dt);
        return;
    }

    if (g.mode == MODE_COURSE) {
        int old = g.course_sel;
        if (g.menu_lock > 0) g.menu_lock--;
        if (pressed & (BTN_LEFT | BTN_L)) g.course_sel = (g.course_sel + TR_NUM_COURSES - 1) % TR_NUM_COURSES;
        if (pressed & (BTN_RIGHT | BTN_R)) g.course_sel = (g.course_sel + 1) % TR_NUM_COURSES;
        if (pressed & BTN_UP) g.course_sel = (g.course_sel + TR_NUM_COURSES - 1) % TR_NUM_COURSES;
        if (pressed & BTN_DOWN) g.course_sel = (g.course_sel + 1) % TR_NUM_COURSES;
        (void)old;
        if (pressed & BTN_A && g.menu_lock == 0) start_race();
        if (pressed & BTN_B) {
            g.mode = MODE_TITLE;
            scene_load(0);
        }
        update_camera(dt);
        return;
    }

    if (g.mode == MODE_CONTROLS || g.mode == MODE_CREDITS) {
        if (pressed & (BTN_A | BTN_B | BTN_START)) g.mode = MODE_TITLE;
        update_camera(dt);
        return;
    }

    if (g.mode == MODE_RESULTS) {
        menu_move(&g.result_sel, 2, pressed);
        if (pressed & BTN_A) {
            if (g.result_sel == 0) start_race();
            else { g.mode = MODE_COURSE; scene_load(g.course_idx); }
        }
        if (pressed & BTN_B) { g.mode = MODE_COURSE; scene_load(g.course_idx); }
        update_camera(dt);
        return;
    }

    if (g.mode == MODE_PAUSE) {
        menu_move(&g.pause_sel, 4, pressed);
        if (pressed & BTN_A) {
            if (g.pause_sel == 0) g.mode = MODE_RACE;
            else if (g.pause_sel == 1) start_race();
            else if (g.pause_sel == 2) { g.mode = MODE_COURSE; g.menu_lock = 10; }
            else { g.mode = MODE_TITLE; scene_load(0); }
        }
        if (pressed & (BTN_B | BTN_START)) g.mode = MODE_RACE;
        update_camera(dt);
        return;
    }

    if (g.mode != MODE_RACE) return;
    if (pressed & BTN_START) {
        g.mode = MODE_PAUSE;
        g.pause_sel = 0;
        tr_audio_play_pause();
        return;
    }
    if (pressed & BTN_SELECT) {
        g.posx = g.respawn_x;
        g.posz = g.respawn_z;
        g.posy = mheight(g.posx, g.posz);
        g.speed = 0;
        g.vy = 0;
        g.grounded = 1;
        g.heading = 0;
        g.race_time += FX(3);
        g.cam_init = 0;
    }

    if (g.countdown > 0) {
        g.countdown -= dt;
        if (g.countdown < 0) g.countdown = 0;
        update_camera(dt);
        return;
    }

    fx jump_pressed = (pressed & BTN_A) ? FX(1) : 0;
    if (jump_pressed && g.grounded) {
        g.vy = FX(6.5);
        g.grounded = 0;
        g.airborne = 1;
        tr_audio_play_jump();
    }
    update_physics(dt);
    if (g.mode == MODE_RACE) update_camera(dt);
    tr_audio_update_slide(g.speed, tr_course_surface(g.posx, g.posz), g.grounded);
}

static void format_time(fx t, char *buf, size_t n)
{
    int64_t cs = ((int64_t)t * 100) / FX_ONE;
    if (cs < 0) cs = 0;
    int m = (int)(cs / 6000);
    int s = (int)((cs / 100) % 60);
    int h = (int)(cs % 100);
    snprintf(buf, n, "%d:%02d.%02d", m, s, h);
}

static void draw_panel(int x, int y, int w, int h)
{
    gfx_rect(x, y, w, h, TR_UI_DARK, 220);
    gfx_rect(x, y, w, 2, TR_WHITE, 170);
    gfx_rect(x, y + h - 2, w, 2, TR_WHITE, 90);
    gfx_rect(x, y, 2, h, TR_WHITE, 90);
    gfx_rect(x + w - 2, y, 2, h, TR_WHITE, 90);
}

static void draw_menu_item(const char *s, int x, int y, int sel, int size)
{
    if (sel) {
        gfx_rect(x - 8, y - 2, gfx_text_width(&tr_font_white, s, size) + 16, size + 6,
                 TR_UI_BLUE, 190);
        gfx_text(&tr_font_yellow, tr_tex_font_yellow, s, x, y, size, TR_WHITE, 0);
        gfx_text(&tr_font_yellow, tr_tex_font_yellow, ">", x - 22, y, size, TR_YELLOW, 0);
    } else {
        gfx_text(&tr_font_white, tr_tex_font_white, s, x, y, size, TR_WHITE, 0);
    }
}

static void wrap_text(const char *s, int x, int y, int width, int size, int line_h)
{
    char buf[200];
    int bi = 0, cx = x, cy = y;
    while (*s) {
        if (*s == '\n') {
            buf[bi] = 0;
            gfx_text(&tr_font_white, tr_tex_font_white, buf, cx, cy, size, 0xE71C, 0);
            bi = 0; s++; cy += line_h;
            continue;
        }
        const char *word = s;
        while (*s && *s != ' ' && *s != '\n') s++;
        int wl = (int)(s - word);
        char wb[64];
        int wn = wl < (int)sizeof(wb) - 1 ? wl : (int)sizeof(wb) - 1;
        memcpy(wb, word, (size_t)wn);
        wb[wn] = 0;
        int ww = gfx_text_width(&tr_font_white, wb, size);
        if (bi > 0) {
            buf[bi] = 0;
            if (gfx_text_width(&tr_font_white, buf, size) +
                gfx_text_width(&tr_font_white, " ", size) + ww > width) {
                gfx_text(&tr_font_white, tr_tex_font_white, buf, cx, cy, size, 0xE71C, 0);
                bi = 0; cy += line_h;
            }
        }
        if (bi > 0) buf[bi++] = ' ';
        for (int i = 0; i < wn && bi < (int)sizeof(buf) - 1; i++) buf[bi++] = wb[i];
        if (bi < (int)sizeof(buf)) buf[bi] = 0;
        if (*s == ' ') s++;
    }
    if (bi) {
        buf[bi] = 0;
        gfx_text(&tr_font_white, tr_tex_font_white, buf, cx, cy, size, 0xE71C, 0);
    }
}

static void draw_hud(void)
{
    tr_course *c = tr_current_course();
    if (!c) return;
    char buf[64];

    gfx_rect(0, 0, TR_W, 28, 0x0000, 100);
    gfx_text(&tr_font_yellow, tr_tex_font_yellow, tr_courses[g.course_idx].name,
             10, 5, 17, TR_WHITE, 0);
    if (tr_tex_icon) gfx_blit(tr_tex_icon, 160, 2, 24, 24, TR_WHITE, 255, 0);
    snprintf(buf, sizeof(buf), "x %d/%d", g.herring, g.herring_total);
    gfx_text(&tr_font_yellow, tr_tex_font_yellow, buf, 186, 5, 17, TR_WHITE, 0);

    format_time(g.race_time, buf, sizeof(buf));
    gfx_text(&tr_font_yellow, tr_tex_font_yellow, buf, 312, 5, 18, TR_WHITE, 2);

    /* speed */
    int kmh = (int)(((int64_t)g.speed * 36) / (FX_ONE * 10));
    snprintf(buf, sizeof(buf), "%d km/h", kmh);
    gfx_rect(TR_W - 92, TR_H - 28, 88, 24, 0x0000, 110);
    gfx_text(&tr_font_yellow, tr_tex_font_yellow, buf, TR_W - 8, TR_H - 26, 17, TR_WHITE, 2);

    /* progress */
    fx prog = 0;
    if (c->length > 0) prog = fx_div(-g.posz, c->length);
    if (prog < 0) prog = 0;
    if (prog > FX(1)) prog = FX(1);
    int pw = (int)((prog * (TR_W - 16)) >> 16);
    gfx_rect(8, TR_H - 8, TR_W - 16, 3, 0x0000, 140);
    gfx_rect(8, TR_H - 8, pw, 3, TR_YELLOW, 220);
}

void tr_game_render(void)
{
    tr_course *c = tr_current_course();
    gfx_frame_begin();
    gfx_sky();

    if (c) {
        gfx_draw_terrain(c, g.posx, g.posz);
        gfx_draw_world_objects(c, g.posx, g.posz, g.race_time,
                               (g.mode == MODE_RACE || g.mode == MODE_PAUSE || g.mode == MODE_RESULTS),
                               g.item_taken);
        gfx_draw_tux_shadow(v3_make(g.posx, g.posy, g.posz));
        fx py = g.posy;
        if (g.mode == MODE_TITLE || g.mode == MODE_COURSE || g.mode == MODE_CONTROLS || g.mode == MODE_CREDITS) {
            py = mheight(g.posx, g.posz);
        }
        int turn = 0;
        if (g.prev_buttons & BTN_RIGHT) turn = 1;
        else if (g.prev_buttons & BTN_LEFT) turn = -1;
        gfx_draw_tux_model(v3_make(g.posx, py, g.posz), g.heading,
                           (ang)g.pitch, (ang)g.roll, g.speed,
                           turn, (g.prev_buttons & BTN_DOWN) != 0,
                           (g.prev_buttons & BTN_UP) != 0, g.airborne);
    } else {
        gfx_text(&tr_font_white, tr_tex_font_white, "Missing game data", 160, 110, 20, TR_WHITE, 1);
    }

    for (int i = 0; i < 48; i++) {
        int a = 110 + (i % 4) * 30;
        gfx_snowflake(g.snow_x[i], g.snow_y[i], g.snow_sz[i], a);
    }

    if (g.mode == MODE_RACE) {
        if (g.countdown > 0) {
            int n = (int)((g.countdown + FX_ONE - 1) >> 16);
            char nb[8];
            snprintf(nb, sizeof(nb), "%d", n);
            gfx_text(&tr_font_yellow, tr_tex_font_yellow, nb, 160, 88, 64, TR_WHITE, 1);
        } else if (g.race_time < FX(0.8)) {
            gfx_text(&tr_font_yellow, tr_tex_font_yellow, "GO!", 160, 90, 54, TR_YELLOW, 1);
        }
        draw_hud();
    } else if (g.mode == MODE_PAUSE) {
        draw_hud();
        gfx_rect(0, 0, TR_W, TR_H, 0x0000, 120);
        draw_panel(70, 45, 180, 155);
        gfx_text(&tr_font_yellow, tr_tex_font_yellow, "PAUSED", 160, 55, 26, TR_WHITE, 1);
        draw_menu_item("RESUME", 100, 88, g.pause_sel == 0, 20);
        draw_menu_item("RESTART", 100, 116, g.pause_sel == 1, 20);
        draw_menu_item("COURSES", 100, 144, g.pause_sel == 2, 20);
        draw_menu_item("QUIT", 100, 172, g.pause_sel == 3, 20);
    } else if (g.mode == MODE_RESULTS) {
        char tb[32], pb[32];
        format_time(g.finish_time, tb, sizeof(tb));
        format_time(tr_courses[g.course_idx].par_tenths * FX_ONE / 10, pb, sizeof(pb));
        gfx_rect(0, 0, TR_W, TR_H, 0x0000, 130);
        draw_panel(28, 26, 264, 188);
        gfx_text(&tr_font_yellow, tr_tex_font_yellow, "FINISHED!", 160, 38, 28,
                 g.finish_new_best ? TR_YELLOW : TR_WHITE, 1);
        if (g.finish_new_best)
            gfx_text(&tr_font_white, tr_tex_font_white, "NEW BEST TIME", 160, 68, 15, TR_YELLOW, 1);
        gfx_text(&tr_font_white, tr_tex_font_white, "TIME", 70, 94, 16, TR_WHITE, 0);
        gfx_text(&tr_font_yellow, tr_tex_font_yellow, tb, 210, 91, 22, TR_WHITE, 2);
        char hb[48];
        snprintf(hb, sizeof(hb), "%d / %d", g.finish_herring, g.herring_total);
        gfx_text(&tr_font_white, tr_tex_font_white, "HERRING", 70, 120, 16, TR_WHITE, 0);
        gfx_text(&tr_font_yellow, tr_tex_font_yellow, hb, 210, 117, 22, TR_WHITE, 2);
        gfx_text(&tr_font_white, tr_tex_font_white, "PAR", 70, 146, 16, TR_WHITE, 0);
        gfx_text(&tr_font_yellow, tr_tex_font_yellow, pb, 210, 143, 22, TR_WHITE, 2);
        draw_menu_item("RETRY", 80, 180, g.result_sel == 0, 18);
        draw_menu_item("COURSES", 180, 180, g.result_sel == 1, 18);
    } else if (g.mode == MODE_TITLE) {
        gfx_rect(0, 0, TR_W, TR_H, 0x0000, 40);
        if (tr_tex_logo)
            gfx_blit(tr_tex_logo, 32, 16, 256, 128, TR_WHITE, 255, 1);
        draw_menu_item("START RACE", 104, 160, g.title_sel == 0, 22);
        draw_menu_item("CONTROLS", 104, 190, g.title_sel == 1, 22);
        draw_menu_item("CREDITS", 104, 220, g.title_sel == 2, 22);
        gfx_text(&tr_font_white, tr_tex_font_white, "GB300 / SF2000", 312, 224, 11, 0xBDF7, 2);
    } else if (g.mode == MODE_COURSE) {
        tr_course_info *ci = &tr_courses[g.course_sel];
        gfx_rect(0, 0, TR_W, TR_H, 0x0000, 100);
        draw_panel(10, 22, 300, 196);
        gfx_text(&tr_font_yellow, tr_tex_font_yellow, "SELECT A COURSE", 160, 28, 20, TR_WHITE, 1);
        if (tr_tex_preview[g.course_sel])
            gfx_blit(tr_tex_preview[g.course_sel], 22, 58, 112, 112, TR_WHITE, 255, 0);
        gfx_text(&tr_font_yellow, tr_tex_font_yellow, ci->name, 146, 56, 22, TR_WHITE, 0);
        char tb[32], pb[48];
        format_time(ci->par_tenths * FX_ONE / 10, tb, sizeof(tb));
        snprintf(pb, sizeof(pb), "PAR  %s", tb);
        gfx_text(&tr_font_white, tr_tex_font_white, pb, 146, 84, 14, TR_WHITE, 0);
        tr_course *cc = tr_current_course();
        int hc = 0;
        if (cc) for (int ii = 0; ii < cc->num_items; ii++) if (cc->items[ii].type == 0) hc++;
        snprintf(pb, sizeof(pb), "HERRING %d", hc);
        gfx_text(&tr_font_white, tr_tex_font_white, pb, 146, 104, 14, TR_WHITE, 0);
        wrap_text(ci->desc, 146, 126, 154, 12, 15);
        gfx_text(&tr_font_yellow, tr_tex_font_yellow, "<", 12, 150, 30, TR_YELLOW, 0);
        gfx_text(&tr_font_yellow, tr_tex_font_yellow, ">", 296, 150, 30, TR_YELLOW, 0);
        gfx_text(&tr_font_white, tr_tex_font_white, "A START   B BACK", 160, 194, 13, TR_WHITE, 1);
        gfx_text(&tr_font_white, tr_tex_font_white, "UP/DOWN OR LEFT/RIGHT", 160, 210, 10, 0xBDF7, 1);
    } else if (g.mode == MODE_CONTROLS) {
        gfx_rect(0, 0, TR_W, TR_H, 0x0000, 150);
        draw_panel(18, 18, 284, 204);
        gfx_text(&tr_font_yellow, tr_tex_font_yellow, "CONTROLS", 160, 26, 24, TR_WHITE, 1);
        gfx_text(&tr_font_white, tr_tex_font_white, "LEFT / RIGHT   steer", 36, 66, 16, TR_WHITE, 0);
        gfx_text(&tr_font_white, tr_tex_font_white, "UP             paddle", 36, 90, 16, TR_WHITE, 0);
        gfx_text(&tr_font_white, tr_tex_font_white, "DOWN           brake", 36, 114, 16, TR_WHITE, 0);
        gfx_text(&tr_font_white, tr_tex_font_white, "A              jump", 36, 138, 16, TR_WHITE, 0);
        gfx_text(&tr_font_white, tr_tex_font_white, "START          pause", 36, 162, 16, TR_WHITE, 0);
        gfx_text(&tr_font_white, tr_tex_font_white, "SELECT         reset to checkpoint", 36, 186, 14, TR_WHITE, 0);
        gfx_text(&tr_font_white, tr_tex_font_white, "A / B          back", 36, 206, 12, 0xBDF7, 0);
    } else if (g.mode == MODE_CREDITS) {
        gfx_rect(0, 0, TR_W, TR_H, 0x0000, 150);
        draw_panel(18, 18, 284, 204);
        gfx_text(&tr_font_yellow, tr_tex_font_yellow, "TUX RACER", 160, 28, 24, TR_WHITE, 1);
        gfx_text(&tr_font_white, tr_tex_font_white, "Original game (GPL):", 30, 64, 14, TR_WHITE, 0);
        gfx_text(&tr_font_white, tr_tex_font_white, "Jasmin F. Patry / Sunspire Studios", 30, 82, 13, TR_WHITE, 0);
        gfx_text(&tr_font_white, tr_tex_font_white, "GB300 / SF2000 port:", 30, 106, 14, TR_WHITE, 0);
        gfx_text(&tr_font_yellow, tr_tex_font_yellow, "Synaps33", 30, 124, 14, TR_YELLOW, 0);
        gfx_text(&tr_font_white, tr_tex_font_white, "software fixed-point renderer", 30, 142, 13, TR_WHITE, 0);
        gfx_text(&tr_font_white, tr_tex_font_white, "320x240, no FPU, GPL-2.0", 30, 160, 13, TR_WHITE, 0);
        gfx_text(&tr_font_white, tr_tex_font_white, "Thanks to the Tux Racer community", 30, 182, 12, 0xBDF7, 0);
        gfx_text(&tr_font_white, tr_tex_font_white, "Press A or B", 160, 206, 13, TR_WHITE, 1);
    }
}

#ifdef TR_TEST_HOOKS
void tr_game_test_finish(void)
{
    if (g.mode == MODE_RACE) {
        g.posz = g.finish_z;
        finish_race();
    }
}
void tr_game_test_near_finish(void)
{
    tr_course *c = tr_current_course();
    if (!c) return;
    g.mode = MODE_RACE;
    g.countdown = 0;
    g.race_time = FX(35);
    g.posz = c->finish_z + FX(25);
    g.posx = c->width / 2;
    g.posy = mheight(g.posx, g.posz);
    g.speed = FX(8);
    g.heading = 0;
    g.cam_init = 0;
    set_camera_now();
}
void tr_game_test_goto_course(int idx)
{
    g.mode = MODE_COURSE;
    g.course_sel = idx;
    scene_load(idx);
}
void tr_game_test_info(int *posz_m, int *herring, int *mode, int *time_cs, int *speed_kmh)
{
    if (posz_m) *posz_m = (int)(g.posz >> 16);
    if (herring) *herring = g.herring;
    if (mode) *mode = (int)g.mode;
    if (time_cs) *time_cs = (int)((int64_t)g.race_time * 100 / FX_ONE);
    if (speed_kmh) *speed_kmh = (int)(((int64_t)g.speed * 36) / (FX_ONE * 10));
}
void tr_game_test_tux_side(void)
{
    tr_course *c = tr_current_course();
    if (!c) return;
    g.mode = MODE_RACE;
    g.countdown = 0;
    g.race_time = FX(2);
    g.speed = 0;
    g.grounded = 1;
    g.airborne = 0;
    g.pitch = 0;
    g.roll = 0;
    g.heading = 0;
    g.posx = c->start_x;
    g.posz = c->start_z;
    g.posy = mheight(g.posx, g.posz);
    g.cam = v3_make(g.posx + FX(4.0), g.posy + FX(1.2), g.posz + FX(1.5));
    g.target = v3_make(g.posx, g.posy + FX(0.6), g.posz);
    g.free_camera = 1;
    g.cam_init = 1;
    gfx_set_camera(g.cam, g.target);
}
void tr_game_test_tux_preview(void)
{
    tr_course *c = tr_current_course();
    if (!c) return;
    g.mode = MODE_RACE;
    g.countdown = 0;
    g.race_time = FX(2);
    g.speed = 0;
    g.vy = 0;
    g.grounded = 1;
    g.airborne = 0;
    g.pitch = 0;
    g.roll = 0;
    g.heading = 0;
    g.posx = c->start_x;
    g.posz = c->start_z;
    g.posy = mheight(g.posx, g.posz);
    g.cam_init = 0;
    set_camera_now();
}
void tr_game_test_debug(void)
{
    tr_log("DBG x=%d z=%d y=%d speed=%d vy=%d grounded=%d air=%d heading=%d surf=%d crash=%d",
           g.posx>>16, g.posz>>16, g.posy>>16, g.speed>>16, g.vy>>16,
           g.grounded, g.airborne, g.heading, tr_course_surface(g.posx,g.posz),
           g.crash_timer>>16);
}
void tr_game_test_teleport_to_herring(void)
{
    tr_course *c = tr_current_course();
    if (!c) return;
    g.mode = MODE_RACE;
    g.countdown = 0;
    g.cam_init = 0;
    for (int i = 0; i < c->num_items; i++) {
        if (c->items[i].type == 0 && !g.item_taken[i & 511]) {
            g.posx = c->items[i].x;
            g.posz = c->items[i].z;
            g.posy = mheight(g.posx, g.posz);
            g.heading = 0;
            g.speed = FX(5);
            g.grounded = 1;
            g.cam_init = 0;
            set_camera_now();
            return;
        }
    }
}
#endif
