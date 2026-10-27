#include "tr_asset.h"
#include <stdarg.h>
#include <sys/stat.h>
#include <unistd.h>

tr_tex *tr_tex_snow, *tr_tex_rock, *tr_tex_ice;
tr_tex *tr_tex_tree[3];
tr_tex *tr_tex_item[5];
tr_tex *tr_tex_font_white, *tr_tex_font_yellow;
tr_tex *tr_tex_logo, *tr_tex_icon, *tr_tex_snowp;
tr_tex *tr_tex_preview[TR_NUM_COURSES];
tr_font tr_font_white, tr_font_yellow;
tr_course_info tr_courses[TR_NUM_COURSES] = {
    { "bunny_hill", "Bunny Hill", "Clever turns conquer the Bunny Hill.", 400 },
    { "twisty_slope", "Twisty Slope", "Tight twists and hard turns.", 400 },
    { "bumpy_ride", "Bumpy Ride", "Hit the ramps and catch air.", 400 },
    { "frozen_river", "Frozen River", "Keep your speed down to collect herring!", 800 },
    { "path_of_daggers", "Path of Daggers", "Take a high or low road to victory.", 700 },
};

static char g_root[512];
static tr_course g_course;
static int g_course_loaded = -1;
static int load_common_textures(void);

static int file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static void dirname_of(const char *path, char *out, size_t n)
{
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    if (bslash > slash) slash = bslash;
    if (!slash) { snprintf(out, n, "."); return; }
    if (slash == path) { snprintf(out, n, "/"); return; }
    size_t len = (size_t)(slash - path);
    if (len >= n) len = n - 1;
    memcpy(out, path, len);
    out[len] = 0;
}

static int make_absolute(const char *path, char *out, size_t n)
{
    if (!path || !*path) return 0;
    if (path[0] == '/' || (path[0] && path[1] == ':')) {
        snprintf(out, n, "%s", path);
        return 1;
    }
    char cwd[384];
    if (!getcwd(cwd, sizeof(cwd))) return 0;
    snprintf(out, n, "%s/%s", cwd, path);
    return 1;
}

static int has_assets(const char *dir)
{
    char p[768];
    snprintf(p, sizeof(p), "%s/courses.idx", dir);
    if (file_exists(p)) return 1;
    snprintf(p, sizeof(p), "%s/data/courses.idx", dir);
    return file_exists(p);
}

static int find_root_from(const char *start, char *out, size_t n)
{
    char cur[768];
    snprintf(cur, sizeof(cur), "%s", start);
    for (int i = 0; i < 6; i++) {
        if (has_assets(cur)) {
            if (file_exists(cur)) {}
            snprintf(out, n, "%s", cur);
            return 1;
        }
        if (strcmp(cur, "/") == 0 || strcmp(cur, ".") == 0) break;
        char parent[768];
        dirname_of(cur, parent, sizeof(parent));
        if (strcmp(parent, cur) == 0) break;
        snprintf(cur, sizeof(cur), "%s", parent);
    }
    return 0;
}

const char *tr_asset_root(void) { return g_root; }

int tr_assets_init(const char *content_path)
{
    char abs[768], dir[768];
    g_root[0] = 0;

    if (content_path && *content_path && make_absolute(content_path, abs, sizeof(abs))) {
        if (has_assets(abs)) {
            snprintf(g_root, sizeof(g_root), "%s", abs);
        } else {
            dirname_of(abs, dir, sizeof(dir));
            if (has_assets(dir)) snprintf(g_root, sizeof(g_root), "%s", dir);
            else find_root_from(dir, g_root, sizeof(g_root));
        }
    }

    if (!g_root[0]) {
        static const char *fallbacks[] = {
            "assets",
            "./assets",
            "/mnt/sda1/ROMS/tuxracer",
            "/mnt/sda1/roms/tuxracer",
            "ROMS/tuxracer",
            "/home/Sajnaps/tuxracer-sf2000/assets",
            NULL
        };
        for (int i = 0; fallbacks[i]; i++) {
            if (has_assets(fallbacks[i])) {
                snprintf(g_root, sizeof(g_root), "%s", fallbacks[i]);
                break;
            }
        }
    }

    if (!g_root[0]) {
        tr_log("Tux Racer: asset root not found (content=%s)", content_path ? content_path : "(null)");
        return 0;
    }

    /* If the root points at the parent of ./data, prefer the data folder. */
    {
        char p[768];
        snprintf(p, sizeof(p), "%s/data/courses.idx", g_root);
        if (!file_exists(p)) {
            char cue[768];
            snprintf(cue, sizeof(cue), "%s/courses.idx", g_root);
            if (!file_exists(cue)) {
                snprintf(p, sizeof(p), "%s/data", g_root);
                if (has_assets(p)) snprintf(g_root, sizeof(g_root), "%s", p);
            }
        }
    }

    if (!load_common_textures()) {
        tr_log("Tux Racer: required textures missing at %s", g_root);
        return 0;
    }
    tr_log("Tux Racer: assets at %s", g_root);
    return 1;
}

static void *read_file(const char *name, size_t *size_out)
{
    char path[768];
    snprintf(path, sizeof(path), "%s/%s", g_root, name);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return NULL; }
    void *buf = malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    if (size_out) *size_out = (size_t)sz;
    return buf;
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int32_t rdi32(const uint8_t *p) { return (int32_t)rd32(p); }

static tr_tex *tex_load_file(const char *name)
{
    size_t sz = 0;
    uint8_t *buf = (uint8_t *)read_file(name, &sz);
    if (!buf) {
        tr_log("Tux Racer: missing texture %s", name);
        return NULL;
    }
    if (sz < 9 || memcmp(buf, "TEX1", 4) != 0) {
        free(buf);
        tr_log("Tux Racer: bad texture %s", name);
        return NULL;
    }
    int w = rd16(buf + 4), h = rd16(buf + 6);
    int fmt = buf[8];
    size_t need = 9 + (size_t)w * h * 2 + (fmt == 1 ? (size_t)w * h : 0);
    if (w <= 0 || h <= 0 || sz < need) {
        free(buf);
        tr_log("Tux Racer: truncated texture %s", name);
        return NULL;
    }
    tr_tex *t = (tr_tex *)malloc(sizeof(*t));
    if (!t) { free(buf); return NULL; }
    t->w = w; t->h = h; t->fmt = (uint8_t)fmt;
    t->px = (uint16_t *)malloc((size_t)w * h * 2);
    if (!t->px) { free(t); free(buf); return NULL; }
    memcpy(t->px, buf + 9, (size_t)w * h * 2);
    t->a = NULL;
    if (fmt == 1) {
        t->a = (uint8_t *)malloc((size_t)w * h);
        if (t->a) memcpy(t->a, buf + 9 + (size_t)w * h * 2, (size_t)w * h);
    }
    free(buf);
    return t;
}

static int font_load(tr_font *f, const char *base)
{
    char name[96];
    snprintf(name, sizeof(name), "font/%s.fnt", base);
    size_t sz = 0;
    uint8_t *buf = (uint8_t *)read_file(name, &sz);
    if (!buf) { tr_log("Tux Racer: missing font %s", base); return 0; }
    if (sz < 24 || memcmp(buf, "FNT1", 4) != 0) { free(buf); return 0; }
    f->texw = (int)rd32(buf + 4);
    f->texh = (int)rd32(buf + 8);
    f->max_ascent = (int)rd32(buf + 12);
    f->max_descent = (int)rd32(buf + 16);
    f->src_scale = (int)rd32(buf + 20);
    if (f->src_scale < 1) f->src_scale = 1;
    memset(f->g, 0, sizeof(f->g));
    const uint8_t *p = buf + 24;
    for (int i = 0; i < 256 && (size_t)(p - buf) + 12 <= sz; i++, p += 12) {
        int ch = rd16(p);
        if (ch < 0 || ch > 255) continue;
        f->g[ch].w = p[2];
        f->g[ch].h = p[3];
        f->g[ch].xo = (int8_t)p[4];
        f->g[ch].yo = (int8_t)p[5];
        f->g[ch].kern = (int8_t)p[6];
        f->g[ch].xp = (int16_t)rd16(p + 8);
        f->g[ch].yp = (int16_t)rd16(p + 10);
    }
    free(buf);
    return 1;
}

static int load_common_textures(void)
{
    tr_tex_snow = tex_load_file("tex/snow.tex");
    tr_tex_rock = tex_load_file("tex/rock.tex");
    tr_tex_ice = tex_load_file("tex/ice.tex");
    tr_tex_tree[0] = tex_load_file("tex/tree1.tex");
    tr_tex_tree[1] = tex_load_file("tex/tree2.tex");
    tr_tex_tree[2] = tex_load_file("tex/tree3.tex");
    tr_tex_item[0] = tex_load_file("tex/herring.tex");
    tr_tex_item[1] = tex_load_file("tex/flag.tex");
    tr_tex_item[2] = tex_load_file("tex/start.tex");
    tr_tex_item[3] = tex_load_file("tex/finish.tex");
    tr_tex_item[4] = NULL;
    tr_tex_font_white = tex_load_file("tex/font_white.tex");
    tr_tex_font_yellow = tex_load_file("tex/font_yellow.tex");
    tr_tex_logo = tex_load_file("tex/logo.tex");
    tr_tex_icon = tex_load_file("tex/herring_icon.tex");
    tr_tex_snowp = tex_load_file("tex/snow_particle.tex");
    for (int i = 0; i < TR_NUM_COURSES; i++) {
        char name[96];
        snprintf(name, sizeof(name), "tex/preview_%s.tex", tr_courses[i].id);
        tr_tex_preview[i] = tex_load_file(name);
    }
    font_load(&tr_font_white, "font_white");
    font_load(&tr_font_yellow, "font_yellow");
    return tr_tex_snow && tr_tex_rock && tr_tex_ice && tr_tex_font_white && tr_tex_font_yellow;
}

void tr_course_unload(void)
{
    free(g_course.heights);
    free(g_course.surfaces);
    free(g_course.trees);
    free(g_course.items);
    memset(&g_course, 0, sizeof(g_course));
    g_course_loaded = -1;
}

tr_course *tr_current_course(void) { return g_course_loaded >= 0 ? &g_course : NULL; }

int tr_course_load(int idx)
{
    if (idx < 0 || idx >= TR_NUM_COURSES) return 0;
    if (g_course_loaded == idx) return 1;
    tr_course_unload();

    char name[96];
    snprintf(name, sizeof(name), "course/%s.trk", tr_courses[idx].id);
    size_t sz = 0;
    uint8_t *buf = (uint8_t *)read_file(name, &sz);
    if (!buf) { tr_log("Tux Racer: missing course %s", name); return 0; }
    if (sz < 58 || memcmp(buf, "TRK1", 4) != 0) { free(buf); return 0; }
    const uint8_t *p = buf + 4;
    int version = rd16(p); p += 2;
    p += 2; /* flags */
    int nx = rd16(p); p += 2;
    int ny = rd16(p); p += 2;
    int width_mm = (int)rd32(p); p += 4;
    int length_mm = (int)rd32(p); p += 4;
    int play_w_mm = (int)rd32(p); p += 4;
    int play_l_mm = (int)rd32(p); p += 4;
    int start_x_mm = rdi32(p); p += 4;
    int start_z_mm = rdi32(p); p += 4;
    int elev_mm = rdi32(p); p += 4;
    int base = (int16_t)rd16(p); p += 2;
    int finish_mm = rdi32(p); p += 4;
    int start_item_mm = rdi32(p); p += 4;
    p += 4; /* reserved */
    int angle_milli = rdi32(p); p += 4;

    if (version != 1 || nx <= 1 || ny <= 1) { free(buf); return 0; }
    size_t heights_need = (size_t)nx * ny * 4;
    size_t surf_need = (size_t)nx * ny;
    if ((size_t)(p - buf) + heights_need + surf_need + 2 > sz) { free(buf); return 0; }

    g_course.heights = (fx *)malloc(heights_need);
    g_course.surfaces = (uint8_t *)malloc(surf_need);
    if (!g_course.heights || !g_course.surfaces) { free(buf); tr_course_unload(); return 0; }

    for (int i = 0; i < nx * ny; i++) {
        int mm = rdi32(p); p += 4;
        g_course.heights[i] = (fx)(((int64_t)mm * 65536) / 1000);
    }
    memcpy(g_course.surfaces, p, surf_need);
    p += surf_need;

    int ntrees = rd16(p); p += 2;
    if ((size_t)(p - buf) + (size_t)ntrees * 18 + 2 > sz) { free(buf); tr_course_unload(); return 0; }
    g_course.trees = (tr_obj *)calloc((size_t)ntrees, sizeof(tr_obj));
    if (!g_course.trees && ntrees) { free(buf); tr_course_unload(); return 0; }
    g_course.num_trees = ntrees;
    for (int i = 0; i < ntrees; i++) {
        tr_obj *o = &g_course.trees[i];
        int x = rdi32(p); p += 4;
        int z = rdi32(p); p += 4;
        o->type = p[0]; o->flags = p[1]; p += 2;
        int h = rdi32(p); p += 4;
        int d = rdi32(p); p += 4;
        o->x = (fx)(((int64_t)x * 65536) / 1000);
        o->z = (fx)(((int64_t)z * 65536) / 1000);
        o->h = (fx)(((int64_t)h * 65536) / 1000);
        o->d = (fx)(((int64_t)d * 65536) / 1000);
    }
    int nitems = rd16(p); p += 2;
    if ((size_t)(p - buf) + (size_t)nitems * 18 > sz) { free(buf); tr_course_unload(); return 0; }
    g_course.items = (tr_obj *)calloc((size_t)nitems, sizeof(tr_obj));
    if (!g_course.items && nitems) { free(buf); tr_course_unload(); return 0; }
    g_course.num_items = nitems;
    for (int i = 0; i < nitems; i++) {
        tr_obj *o = &g_course.items[i];
        int x = rdi32(p); p += 4;
        int z = rdi32(p); p += 4;
        o->type = p[0]; o->flags = p[1]; p += 2;
        int h = (int16_t)rd16(p); p += 2;
        int d = (int16_t)rd16(p); p += 2;
        int ag = (int16_t)rd16(p); p += 2;
        p += 2;
        o->x = (fx)(((int64_t)x * 65536) / 1000);
        o->z = (fx)(((int64_t)z * 65536) / 1000);
        o->h = (fx)(((int64_t)h * 65536) / 1000);
        o->d = (fx)(((int64_t)d * 65536) / 1000);
        o->above = (fx)(((int64_t)ag * 65536) / 1000);
    }

    g_course.nx = nx; g_course.ny = ny;
    g_course.width = (fx)(((int64_t)width_mm * 65536) / 1000);
    g_course.length = (fx)(((int64_t)length_mm * 65536) / 1000);
    g_course.play_width = (fx)(((int64_t)play_w_mm * 65536) / 1000);
    g_course.play_length = (fx)(((int64_t)play_l_mm * 65536) / 1000);
    g_course.start_x = (fx)(((int64_t)start_x_mm * 65536) / 1000);
    g_course.start_z = (fx)(((int64_t)start_z_mm * 65536) / 1000);
    g_course.elev_scale = (fx)(((int64_t)elev_mm * 65536) / 1000);
    g_course.finish_z = (fx)(((int64_t)finish_mm * 65536) / 1000);
    g_course.start_item_z = (fx)(((int64_t)start_item_mm * 65536) / 1000);
    g_course.angle_rad = (fx)(((int64_t)angle_milli * 65536) / 1000);
    (void)base;
    free(buf);
    g_course_loaded = idx;
    tr_log("Tux Racer: loaded %s (%dx%d, %d trees, %d items)", tr_courses[idx].name,
           nx, ny, ntrees, nitems);
    return 1;
}

fx tr_course_height(fx x, fx z)
{
    if (g_course_loaded < 0) return 0;
    int nx = g_course.nx, ny = g_course.ny;
    fx gx = fx_mul(x, fx_div((fx)((nx - 1) << 16), g_course.width));
    fx gz = fx_mul(-z, fx_div((fx)((ny - 1) << 16), g_course.length));
    if (gx < 0) gx = 0;
    if (gz < 0) gz = 0;
    fx maxgx = (fx)((nx - 1) << 16), maxgz = (fx)((ny - 1) << 16);
    if (gx > maxgx - 1) gx = maxgx - 1;
    if (gz > maxgz - 1) gz = maxgz - 1;
    int ix = gx >> 16, iy = gz >> 16;
    fx fxr = gx & 0xffff, fzr = gz & 0xffff;
    fx h00 = g_course.heights[iy * nx + ix];
    fx h10 = g_course.heights[iy * nx + ix + 1];
    fx h01 = g_course.heights[(iy + 1) * nx + ix];
    fx h11 = g_course.heights[(iy + 1) * nx + ix + 1];
    fx a = h00 + fx_mul(h10 - h00, fxr);
    fx b = h01 + fx_mul(h11 - h01, fxr);
    return a + fx_mul(b - a, fzr);
}

int tr_course_surface(fx x, fx z)
{
    if (g_course_loaded < 0) return 2;
    int nx = g_course.nx, ny = g_course.ny;
    fx gx = fx_div(x, g_course.width) * (nx - 1);
    fx gz = fx_div(-z, g_course.length) * (ny - 1);
    int ix = fx_int(gx), iy = fx_int(gz);
    if (ix < 0) ix = 0;
    if (iy < 0) iy = 0;
    if (ix >= nx) ix = nx - 1;
    if (iy >= ny) iy = ny - 1;
    return g_course.surfaces[iy * nx + ix];
}

v3 tr_course_normal(fx x, fx z)
{
    fx d = FX_ONE / 2;
    fx hx1 = tr_course_height(x + d, z);
    fx hx0 = tr_course_height(x - d, z);
    fx hz1 = tr_course_height(x, z + d);
    fx hz0 = tr_course_height(x, z - d);
    v3 n = v3_make(-(hx1 - hx0), 2 * d, -(hz1 - hz0));
    return v3_norm(n);
}

void tr_assets_shutdown(void)
{
    tr_course_unload();
    tr_tex *list[] = {
        tr_tex_snow, tr_tex_rock, tr_tex_ice,
        tr_tex_tree[0], tr_tex_tree[1], tr_tex_tree[2],
        tr_tex_item[0], tr_tex_item[1], tr_tex_item[2], tr_tex_item[3],
        tr_tex_font_white, tr_tex_font_yellow, tr_tex_logo, tr_tex_icon, tr_tex_snowp,
        tr_tex_preview[0], tr_tex_preview[1], tr_tex_preview[2], tr_tex_preview[3], tr_tex_preview[4],
        NULL
    };
    for (int i = 0; list[i]; i++) {
        free(list[i]->px);
        free(list[i]->a);
        free(list[i]);
    }
    tr_tex_snow = tr_tex_rock = tr_tex_ice = NULL;
    tr_tex_tree[0] = tr_tex_tree[1] = tr_tex_tree[2] = NULL;
    for (int i = 0; i < 5; i++) tr_tex_item[i] = NULL;
    tr_tex_font_white = tr_tex_font_yellow = NULL;
    tr_tex_logo = tr_tex_icon = tr_tex_snowp = NULL;
    for (int i = 0; i < TR_NUM_COURSES; i++) tr_tex_preview[i] = NULL;
}
