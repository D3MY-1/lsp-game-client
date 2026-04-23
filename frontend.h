#include "state.h"

#ifndef FRONTEND_H
#define FRONTEND_H

typedef enum {
  ACTION_NONE,
  ACTION_UP,
  ACTION_DOWN,
  ACTION_LEFT,
  ACTION_RIGHT,
  ACTION_BOMB,
  ACTION_READY,
  ACTION_QUIT
} GameAction;

bool frontend_init(void);

void frontend_cleanup(void);

GameAction frontend_handle_input(void);

void frontend_draw(const GameState *game);

void frontend_sleep_until_next_frame(void);

#endif // !FRONTEND
