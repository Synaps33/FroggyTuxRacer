#ifndef TR_GAME_H
#define TR_GAME_H

#include "tr.h"

typedef struct {
    unsigned up, down, left, right;
    unsigned a, b, x, y, l, r, start, select;
} tr_input;

void tr_game_init(void);
void tr_game_shutdown(void);
void tr_game_update(const tr_input *in, uint32_t dt_ms);
void tr_game_render(void);
int tr_game_wants_quit(void);

#ifdef TR_TEST_HOOKS
void tr_game_test_finish(void);
void tr_game_test_near_finish(void);
void tr_game_test_goto_course(int idx);
void tr_game_test_info(int *posz_m, int *herring, int *mode, int *time_cs, int *speed_kmh);
void tr_game_test_teleport_to_herring(void);
void tr_game_test_debug(void);
void tr_game_test_tux_preview(void);
void tr_game_test_tux_side(void);
#endif

#endif
