#include "libretro.h"
#include "tr_asset.h"
#include "tr_audio.h"
#include "tr_game.h"
#include "tr_gfx.h"

#include <string.h>

static retro_environment_t env_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static int loaded;
static int16_t audio_buf[735 * 2];
static unsigned frame_no;

void retro_set_environment(retro_environment_t cb) { env_cb = cb; }
void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { (void)cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

void retro_init(void)
{
    if (env_cb) {
        enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
        env_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);
    }
}

void retro_deinit(void)
{
    if (loaded) {
        tr_game_shutdown();
        loaded = 0;
    }
}

unsigned retro_api_version(void) { return RETRO_API_VERSION; }

void retro_get_system_info(struct retro_system_info *info)
{
    memset(info, 0, sizeof(*info));
    info->library_name = "Tux Racer";
    info->library_version = TR_VERSION "-sf2000";
    info->valid_extensions = "gba";
    info->need_fullpath = true;
    info->block_extract = false;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
    info->geometry.base_width = TR_W;
    info->geometry.base_height = TR_H;
    info->geometry.max_width = TR_W;
    info->geometry.max_height = TR_H;
    info->geometry.aspect_ratio = 4.0f / 3.0f;
    info->timing.fps = 60.0;
    info->timing.sample_rate = 44100.0;
}

void retro_set_controller_port_device(unsigned port, unsigned device) { (void)port; (void)device; }
void retro_reset(void) {}
size_t retro_serialize_size(void) { return 0; }
bool retro_serialize(void *data, size_t size) { (void)data; (void)size; return false; }
bool retro_unserialize(const void *data, size_t size) { (void)data; (void)size; return false; }
void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char *code) { (void)index; (void)enabled; (void)code; }

bool retro_load_game(const struct retro_game_info *game)
{
    const char *path = game ? game->path : NULL;
    if (!tr_assets_init(path)) {
        tr_log("Tux Racer: cannot initialise assets");
        return false;
    }
    tr_game_init();
    loaded = 1;
    frame_no = 0;
    return true;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num) { (void)type; (void)info; (void)num; return false; }
void retro_unload_game(void)
{
    if (loaded) {
        tr_game_shutdown();
        loaded = 0;
    }
}
unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }
void *retro_get_memory_data(unsigned id) { (void)id; return NULL; }
size_t retro_get_memory_size(unsigned id) { (void)id; return 0; }

static uint32_t next_dt(void)
{
    static const uint8_t pattern[3] = { 16, 17, 17 };
    return pattern[frame_no % 3];
}

static void read_input(tr_input *in)
{
    memset(in, 0, sizeof(*in));
    if (!input_state_cb) return;
    unsigned p = 0;
    in->b = input_state_cb(p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
    in->y = input_state_cb(p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
    in->select = input_state_cb(p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT);
    in->start = input_state_cb(p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);
    in->up = input_state_cb(p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);
    in->down = input_state_cb(p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
    in->left = input_state_cb(p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
    in->right = input_state_cb(p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
    in->a = input_state_cb(p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
    in->x = input_state_cb(p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
    in->l = input_state_cb(p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);
    in->r = input_state_cb(p, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);
}

void retro_run(void)
{
    if (!loaded) return;
    if (input_poll_cb) input_poll_cb();
    tr_input in;
    read_input(&in);
    uint32_t dt = next_dt();
#if defined(SF2000) || defined(GB300V2)
    {
        static uint32_t last;
        uint32_t now = tr_ticks_ms();
        if (last && now > last) {
            uint32_t d = now - last;
            if (d < 10) d = 10;
            if (d > 50) d = 50;
            dt = d;
        }
        last = now;
    }
#endif
    tr_game_update(&in, dt);
    tr_game_render();
    if (video_cb)
        video_cb(gfx_fb(), TR_W, TR_H, TR_W * sizeof(uint16_t));
    tr_audio_render(audio_buf, 735);
    if (audio_batch_cb)
        audio_batch_cb(audio_buf, 735);
    frame_no++;
}
