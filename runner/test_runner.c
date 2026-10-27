#include "libretro.h"
#include "tr_game.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 320
#define H 240

static uint16_t fb[W * H];
static unsigned pad;
static int frame_no;
static char prefix[512] = "screenshots/frame";
static long audio_nonzero;
static long audio_frames;
static FILE *audio_dump;

static void video_cb(const void *data, unsigned width, unsigned height, size_t pitch)
{
    if (!data) return;
    const uint8_t *src = (const uint8_t *)data;
    for (unsigned y = 0; y < height && y < H; y++)
        memcpy(&fb[y * W], src + y * pitch, (width < W ? width : W) * 2);
}

static void audio_sample_cb(int16_t l, int16_t r) { (void)l; (void)r; }

static size_t audio_batch_cb(const int16_t *data, size_t frames)
{
    audio_frames += (long)frames;
    if (data) {
        for (size_t i = 0; i < frames * 2; i++)
            if (data[i]) audio_nonzero++;
        if (!audio_dump && getenv("TUXRACER_AUDIO_DUMP"))
            audio_dump = fopen(getenv("TUXRACER_AUDIO_DUMP"), "wb");
        if (audio_dump) fwrite(data, sizeof(int16_t) * 2, frames, audio_dump);
    }
    return frames;
}

static void input_poll_cb(void) {}

static int16_t input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id)
{
    (void)port; (void)device; (void)index;
    return (pad & (1u << id)) ? 1 : 0;
}

static bool environ_cb(unsigned cmd, void *data)
{
    if (cmd == RETRO_ENVIRONMENT_SET_PIXEL_FORMAT) {
        enum retro_pixel_format fmt = *(enum retro_pixel_format *)data;
        return fmt == RETRO_PIXEL_FORMAT_RGB565;
    }
    return true;
}

static int unique_colors(void)
{
    static uint8_t seen[65536];
    memset(seen, 0, sizeof(seen));
    int n = 0;
    for (int i = 0; i < W * H; i++) {
        if (!seen[fb[i]]) { seen[fb[i]] = 1; n++; }
    }
    return n;
}

static int ink_pixels(void)
{
    int n = 0;
    for (int i = 0; i < W * H; i++) if (fb[i]) n++;
    return n;
}

static uint32_t checksum(void)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < W * H; i++) {
        h ^= fb[i];
        h *= 16777619u;
    }
    return h;
}

static void save_ppm(const char *tag)
{
    char path[640];
    snprintf(path, sizeof(path), "%s_%s.ppm", prefix, tag);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        uint16_t p = fb[i];
        unsigned r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
        uint8_t rgb[3] = {
            (uint8_t)((r << 3) | (r >> 2)),
            (uint8_t)((g << 2) | (g >> 4)),
            (uint8_t)((b << 3) | (b >> 2))
        };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("[RUNNER] frame %4d %-16s colors=%3d ink=%6d hash=%08x -> %s\n",
           frame_no, tag, unique_colors(), ink_pixels(), checksum(), path);
}

static void frames(int n)
{
    for (int i = 0; i < n; i++) {
        retro_run();
        frame_no++;
    }
}

static void press(int id, int hold)
{
    pad |= (1u << id);
    frames(hold);
    pad &= ~(1u << id);
    frames(2);
}

int main(int argc, char **argv)
{
    const char *content = argc > 1 ? argv[1] : "assets/TuxRacer.gba";
    if (argc > 2) snprintf(prefix, sizeof(prefix), "%s", argv[2]);

    retro_set_environment(environ_cb);
    retro_set_video_refresh(video_cb);
    retro_set_audio_sample(audio_sample_cb);
    retro_set_audio_sample_batch(audio_batch_cb);
    retro_set_input_poll(input_poll_cb);
    retro_set_input_state(input_state_cb);
    retro_init();

    struct retro_game_info info;
    memset(&info, 0, sizeof(info));
    info.path = content;
    if (!retro_load_game(&info)) {
        fprintf(stderr, "[RUNNER] load failed for %s\n", content);
        return 1;
    }

    if (getenv("TUXRACER_SELECT_TEST")) {
        frames(2);
        press(RETRO_DEVICE_ID_JOYPAD_A, 2);
        frames(6);
        press(RETRO_DEVICE_ID_JOYPAD_RIGHT, 2);
        frames(6);
        save_ppm("select_course2");
        press(RETRO_DEVICE_ID_JOYPAD_A, 2);
        frames(30);
        save_ppm("select_race2");
        int pz, her, mode, tc, kmh;
        tr_game_test_info(&pz, &her, &mode, &tc, &kmh);
        printf("[RUNNER] select z=%d mode=%d time=%d.%02d\n", pz, mode, tc / 100, tc % 100);
        return 0;
    }
    if (getenv("TUXRACER_FISH_TEST")) {
        tr_game_test_goto_course(0);
        frames(2);
        press(RETRO_DEVICE_ID_JOYPAD_A, 2);
        frames(3);
        tr_game_test_teleport_to_herring();
        frames(3);
        save_ppm("fish_collected");
        int pz, her, mode, tc, kmh;
        tr_game_test_info(&pz, &her, &mode, &tc, &kmh);
        printf("[RUNNER] fish herring=%d mode=%d z=%d\n", her, mode, pz);
        return 0;
    }
    if (getenv("TUXRACER_TUX_SIDE")) {
        tr_game_test_goto_course(0);
        frames(2);
        press(RETRO_DEVICE_ID_JOYPAD_A, 2);
        frames(3);
        tr_game_test_tux_side();
        frames(3);
        save_ppm("tux_side");
        return 0;
    }

    if (getenv("TUXRACER_TUX_PREVIEW")) {
        tr_game_test_goto_course(0);
        frames(2);
        press(RETRO_DEVICE_ID_JOYPAD_A, 2);
        frames(3);
        tr_game_test_tux_preview();
        frames(3);
        save_ppm("tux_preview");
        return 0;
    }

    if (getenv("TUXRACER_COURSE_SHOTS")) {
        char name[32];
        for (int i = 0; i < 5; i++) {
            tr_game_test_goto_course(i);
            frames(2);
            press(RETRO_DEVICE_ID_JOYPAD_A, 2);
            frames(30);
            snprintf(name, sizeof(name), "course_%d", i);
            save_ppm(name);
        }
        return 0;
    }

    if (getenv("TUXRACER_SOAK")) {
        int ci = getenv("TUXRACER_COURSE") ? atoi(getenv("TUXRACER_COURSE")) : 0;
        tr_game_test_goto_course(ci);
        frames(2);
        press(RETRO_DEVICE_ID_JOYPAD_A, 2);
        frames(5);
        int pz = 0, her = 0, mode = 0, tc = 0, kmh = 0;
        for (int i = 0; i < 4200; i++) {
            pad = (1u << RETRO_DEVICE_ID_JOYPAD_UP);
            retro_run();
            frame_no++;
            tr_game_test_info(&pz, &her, &mode, &tc, &kmh);
            if ((i % 600) == 0) {
                printf("[RUNNER] soak c=%d f=%d z=%d herring=%d mode=%d time=%d.%02d speed=%d\n",
                       ci, frame_no, pz, her, mode, tc / 100, tc % 100, kmh);
                if (kmh == 0) tr_game_test_debug();
            }
            if (mode == 4) break; /* results */
        }
        tr_game_test_info(&pz, &her, &mode, &tc, &kmh);
        printf("[RUNNER] soak final c=%d z=%d herring=%d mode=%d time=%d.%02d speed=%d\n",
               ci, pz, her, mode, tc / 100, tc % 100, kmh);
        return 0;
    }

    frames(2);
    save_ppm("01_title");
    frames(30);
    save_ppm("02_title_snow");

    press(RETRO_DEVICE_ID_JOYPAD_A, 2);
    frames(6);
    save_ppm("03_course_select");

    press(RETRO_DEVICE_ID_JOYPAD_RIGHT, 2);
    frames(6);
    save_ppm("04_course_twisty");

    press(RETRO_DEVICE_ID_JOYPAD_LEFT, 2);
    frames(5);

    press(RETRO_DEVICE_ID_JOYPAD_A, 2);
    frames(25);
    save_ppm("05_countdown");

    frames(160);
    save_ppm("06_race_start");
    {
        int pz, her, mode, tc, kmh;
        tr_game_test_info(&pz, &her, &mode, &tc, &kmh);
        printf("[RUNNER] race position z=%d herring=%d time=%d.%02d speed=%d\n",
               pz, her, tc / 100, tc % 100, kmh);
    }

    pad = (1u << RETRO_DEVICE_ID_JOYPAD_UP) | (1u << RETRO_DEVICE_ID_JOYPAD_RIGHT);
    frames(40);
    pad = 0;
    frames(10);
    save_ppm("07_race_steer");

    press(RETRO_DEVICE_ID_JOYPAD_A, 2);
    frames(7);
    save_ppm("08_jump");

    pad = (1u << RETRO_DEVICE_ID_JOYPAD_LEFT);
    frames(30);
    pad = 0;
    frames(10);
    save_ppm("09_race_left");

    press(RETRO_DEVICE_ID_JOYPAD_START, 2);
    frames(3);
    save_ppm("10_pause");
    press(RETRO_DEVICE_ID_JOYPAD_START, 2);
    frames(3);

    tr_game_test_near_finish();
    frames(4);
    save_ppm("11_near_finish");
    tr_game_test_finish();
    frames(4);
    save_ppm("12_results");

    press(RETRO_DEVICE_ID_JOYPAD_B, 2);
    frames(4);
    save_ppm("13_course_back");

    press(RETRO_DEVICE_ID_JOYPAD_B, 2);
    frames(4);
    save_ppm("14_title_back");

    printf("[RUNNER] frames=%d audio_frames=%ld audio_nonzero=%ld\n",
           frame_no, audio_frames, audio_nonzero);
    if (audio_dump) fclose(audio_dump);
    retro_unload_game();
    retro_deinit();
    return 0;
}
