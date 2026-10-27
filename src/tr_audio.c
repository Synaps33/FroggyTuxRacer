#include "tr_audio.h"
#include "tr_asset.h"

typedef struct {
    int16_t *data;
    int frames;
    int rate;
} snd_t;

#define NUM_CH 6
typedef struct {
    snd_t *snd;
    int64_t pos;    /* 16.16 source frame */
    int step;
    int vol;
    int loop;
    int active;
} chan_t;

static snd_t s_fish[3], s_hit, s_snow, s_rock, s_ice;
static snd_t s_jump;
static int16_t jump_samples[4410];
static chan_t chans[NUM_CH];
static int inited;
static int slide_chan = -1;
static snd_t *slide_current;

static int load_snd(const char *name, snd_t *s)
{
    char path[768];
    snprintf(path, sizeof(path), "%s/sfx/%s.snd", tr_asset_root(), name);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char magic[4];
    uint32_t frames = 0, rate = 0;
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "SND1", 4) != 0) { fclose(f); return 0; }
    if (fread(&frames, 4, 1, f) != 1 || fread(&rate, 4, 1, f) != 1) { fclose(f); return 0; }
    if (frames == 0 || frames > 1000000) { fclose(f); return 0; }
    s->data = (int16_t *)malloc(frames * 2);
    if (!s->data) { fclose(f); return 0; }
    if (fread(s->data, 2, frames, f) != frames) {
        free(s->data); s->data = NULL; fclose(f); return 0;
    }
    fclose(f);
    s->frames = (int)frames;
    s->rate = (int)rate;
    return 1;
}

static void make_jump(void)
{
    int n = sizeof(jump_samples) / sizeof(jump_samples[0]);
    int phase = 0;
    for (int i = 0; i < n; i++) {
        int env = 22000 - (i * 22000 / n);
        int tri = phase < 16384 ? phase : 32768 - phase;
        jump_samples[i] = (int16_t)(((tri - 16384) * env) >> 16);
        phase = (phase + 1400 - i / 2) & 65535;
    }
    s_jump.data = jump_samples;
    s_jump.frames = n;
    s_jump.rate = 44100;
}

void tr_audio_init(void)
{
    if (inited) return;
    inited = 1;
    memset(chans, 0, sizeof(chans));
    load_snd("fish_pickup1", &s_fish[0]);
    load_snd("fish_pickup2", &s_fish[1]);
    load_snd("fish_pickup3", &s_fish[2]);
    load_snd("tux_hit_tree1", &s_hit);
    load_snd("tux_on_snow1", &s_snow);
    load_snd("tux_on_rock1", &s_rock);
    load_snd("tux_on_ice1", &s_ice);
    make_jump();
    slide_chan = -1;
    slide_current = NULL;
    tr_log("Tux Racer: audio fish=%d hit=%d snow=%d rock=%d ice=%d",
           s_fish[0].data != NULL, s_hit.data != NULL, s_snow.data != NULL,
           s_rock.data != NULL, s_ice.data != NULL);
}

void tr_audio_shutdown(void)
{
    for (int i = 0; i < 3; i++) { free(s_fish[i].data); s_fish[i].data = NULL; }
    free(s_hit.data); s_hit.data = NULL;
    free(s_snow.data); s_snow.data = NULL;
    free(s_rock.data); s_rock.data = NULL;
    free(s_ice.data); s_ice.data = NULL;
    inited = 0;
}

static void play(int ch, snd_t *s, int vol, int loop)
{
    if (!s || !s->data) return;
    chan_t *c = &chans[ch];
    c->snd = s;
    c->pos = 0;
    c->step = (int)(((int64_t)s->rate << 16) / 44100);
    c->vol = vol;
    c->loop = loop;
    c->active = 1;
}

void tr_audio_play_herring(void)
{
    static int n;
    int idx = n++ % 3;
    play(1 + idx, &s_fish[idx], 9000, 0);
}
void tr_audio_play_hit(void) { play(4, &s_hit, 13000, 0); }
void tr_audio_play_jump(void) { play(5, &s_jump, 7000, 0); }
void tr_audio_play_pause(void) { }
void tr_audio_play_race_start(void) { play(5, &s_fish[0], 8000, 0); }

void tr_audio_update_slide(fx speed, int surface, int grounded)
{
    (void)surface;
    if (!inited) return;
    snd_t *want = NULL;
    if (grounded && speed > FX(2)) {
        if (surface == 0) want = &s_ice;
        else if (surface == 1) want = &s_rock;
        else want = &s_snow;
    }
    if (want != slide_current) {
        if (slide_chan >= 0) chans[slide_chan].active = 0;
        slide_current = want;
        if (want && want->data) {
            slide_chan = 0;
            play(0, want, 5000, 1);
        } else {
            slide_chan = -1;
        }
    }
    if (slide_chan >= 0 && want) {
        int v = 1200 + (int)(speed >> 10);
        if (v > 7000) v = 7000;
        chans[slide_chan].vol = v;
    }
}

void tr_audio_render(int16_t *buf, int frames)
{
    if (!inited) {
        memset(buf, 0, (size_t)frames * 2 * sizeof(int16_t));
        return;
    }
    for (int i = 0; i < frames; i++) {
        int32_t mix = 0;
        for (int c = 0; c < NUM_CH; c++) {
            chan_t *ch = &chans[c];
            if (!ch->active || !ch->snd) continue;
            snd_t *s = ch->snd;
            int p = (int)(ch->pos >> 16);
            if (p < 0 || p >= s->frames) {
                if (ch->loop) { ch->pos = 0; p = 0; }
                else { ch->active = 0; continue; }
            }
            int smp = s->data[p];
            mix += (smp * ch->vol) >> 15;
            ch->pos += ch->step;
        }
        if (mix > 32767) mix = 32767;
        if (mix < -32768) mix = -32768;
        buf[i * 2] = (int16_t)mix;
        buf[i * 2 + 1] = (int16_t)mix;
    }
}
