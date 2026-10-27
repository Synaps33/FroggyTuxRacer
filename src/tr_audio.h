#ifndef TR_AUDIO_H
#define TR_AUDIO_H

#include "tr.h"

void tr_audio_init(void);
void tr_audio_shutdown(void);
void tr_audio_play_herring(void);
void tr_audio_play_hit(void);
void tr_audio_play_jump(void);
void tr_audio_play_pause(void);
void tr_audio_play_race_start(void);
void tr_audio_update_slide(fx speed, int surface, int grounded);
void tr_audio_render(int16_t *buf, int frames);

#endif
