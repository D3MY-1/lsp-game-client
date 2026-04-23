#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "frontend.h"
#include "state.h"

int main(void) {
  frontend_init();

  GameState game = {};

  player_t *p = &game.players[0];

  game.map_height = 100;

  game.map_width = 100;

  p->row = 10;
  p->col = 10;
  p->alive = true;

  memset(game.map, '.', MAX_GRID_SIZE * MAX_GRID_SIZE);

  for (int y = 0; y < game.map_height; y++) {
    for (int x = 0; x < game.map_width; x++) {
      int index = y * game.map_width + x;

      // If both X and Y are odd numbers, make it a Hard Wall
      if (y % 2 != 0 && x % 2 != 0) {
        game.map[index] = 'H';
      } else {
        game.map[index] = '.';
      }
    }
  }

  game.status = GAME_RUNNING;

  bool should_quit = false;

  while (!should_quit) {
    GameAction action = frontend_handle_input();

    switch (action) {
    case ACTION_UP:
      game.players[0].row -= 1;
      break;
    case ACTION_DOWN:
      game.players[0].row += 1;
      break;
    case ACTION_LEFT:
      game.players[0].col -= 1;
      break;
    case ACTION_RIGHT:
      game.players[0].col += 1;
      break;
    case ACTION_QUIT:
      should_quit = true;
      break;
    default:
      break;
    }

    frontend_draw(&game);

    frontend_sleep_until_next_frame();
  }

  frontend_cleanup();
}
