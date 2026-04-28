#include "frontend.h"
#include "state.h"
#include <locale.h>
#include <ncurses.h>
#include <stdbool.h>
#include <stdio.h>

static const int player_color[MAX_PLAYERS] = {
    COLOR_BLACK,   COLOR_CYAN,  COLOR_BLUE,   COLOR_GREEN,
    COLOR_MAGENTA, COLOR_WHITE, COLOR_YELLOW, COLOR_RED};

bool frontend_init(void) {

  setlocale(LC_ALL, "");
  initscr();
  cbreak();
  noecho();
  curs_set(0);
  keypad(stdscr, TRUE);
  nodelay(stdscr, TRUE);

  if (!has_colors()) {
    return false;
  }

  start_color();

  init_pair(1, player_color[0], COLOR_WHITE);

  for (int player_id = 1; player_id < MAX_PLAYERS; player_id++) {
    init_pair((short)(player_id + 1), (short)player_color[player_id],
              COLOR_BLACK);
  }

  // Tile color pairs (starting from 10 to avoid player conflicts)
  init_pair(10, COLOR_RED, COLOR_BLACK);    // Explosion '*'
  init_pair(11, COLOR_YELLOW, COLOR_BLACK); // Bomb 'B'
  init_pair(12, COLOR_WHITE, COLOR_WHITE);  // Hard wall 'H'
  init_pair(13, COLOR_YELLOW, COLOR_WHITE); // Soft wall 'S'
  init_pair(14, COLOR_GREEN, COLOR_BLACK);  // Bonus items

  return true;
}

int get_player_color_pair_id(int player_id) { return player_id + 1; }

char player_symbols[MAX_PLAYERS] = {'1', '2', '3', '4', '5', '6', '7', '8'};

#define set_player_color(win, idx) wattron((win), COLOR_PAIR((idx) + 1))

#define unset_player_color(win, idx) wattroff((win), COLOR_PAIR((idx) + 1))

void draw_gameplay(const GameState *game) {

  for (int player_id = 0; player_id < MAX_PLAYERS; player_id++) {
    const player_t *player = &game->players[player_id];

    if (!player->alive) {
      continue;
    }

    set_player_color(stdscr, player_id);
    mvaddch(game->players[player_id].row, game->players[player_id].col,
            player_symbols[player_id]);
    unset_player_color(stdscr, player_id);
  }
  mvprintw(game->map_height, 0, "P1: WASD q: quit");
}

typedef struct {
  WINDOW *map_win;
  int screen_h, screen_w;
  int view_h, view_w;
  bool needs_reinit;
} NcursesCtx;

static NcursesCtx ctx = {.map_win = NULL, .needs_reinit = true};

static void recalculate_dimensions(void) {
  getmaxyx(stdscr, ctx.screen_h, ctx.screen_w);

  ctx.view_h = ctx.screen_h - 6;
  ctx.view_w = ctx.screen_w - 4;

  if (ctx.view_h > 25)
    ctx.view_h = 25;
  if (ctx.view_w > 80)
    ctx.view_w = 80;

  if (ctx.map_win) {
    delwin(ctx.map_win);
    ctx.map_win = NULL;
  }

  int start_y = (ctx.screen_h - (ctx.view_h + 2)) / 2;
  int start_x = (ctx.screen_w - (ctx.view_w + 2)) / 2;
  ctx.map_win = newwin(ctx.view_h + 2, ctx.view_w + 2, start_y, start_x);

  ctx.needs_reinit = false;
}

static void draw_lobby(const GameState *game) {
  if (ctx.needs_reinit || is_term_resized(ctx.screen_h, ctx.screen_w)) {
    recalculate_dimensions();
    erase();
  }

  werase(ctx.map_win);
  box(ctx.map_win, 0, 0);

  wattron(ctx.map_win, A_BOLD);
  mvwprintw(ctx.map_win, 1, 2, "=== BOMBERMAN LOBBY ===");
  wattroff(ctx.map_win, A_BOLD);

  mvwprintw(ctx.map_win, 3, 2, "Server: %.20s", game->server_name);
  mvwprintw(ctx.map_win, 4, 2, "Your ID: %d", game->my_player_id);

  mvwprintw(ctx.map_win, 6, 2, "Players:");

  int line = 7;
  for (int player_id = 0; player_id < MAX_PLAYERS; player_id++) {
    const player_t *p = &game->players[player_id];
    if (!p->is_connected)
      continue;

    set_player_color(ctx.map_win, player_id);
    mvwprintw(ctx.map_win, line, 4, "[%d] %-30s %s", player_id, p->name,
              p->ready ? "[READY]" : "");
    unset_player_color(ctx.map_win, player_id);
    line++;
  }

  mvwprintw(ctx.map_win, line + 2, 2, "Press [R] to toggle ready");
  mvwprintw(ctx.map_win, line + 3, 2, "Press [Q] to quit");

  wnoutrefresh(stdscr);
  wnoutrefresh(ctx.map_win);
  doupdate();
}

static void draw_game_end(const GameState *game) {
  if (ctx.needs_reinit || is_term_resized(ctx.screen_h, ctx.screen_w)) {
    recalculate_dimensions();
    erase();
  }

  werase(ctx.map_win);
  box(ctx.map_win, 0, 0);

  wattron(ctx.map_win, A_BOLD);
  mvwprintw(ctx.map_win, 1, 2, "=== GAME OVER ===");
  wattroff(ctx.map_win, A_BOLD);

  if (game->winner_id < MAX_PLAYERS) {
    const player_t *winner = &game->players[game->winner_id];
    set_player_color(ctx.map_win, game->winner_id);
    mvwprintw(ctx.map_win, 3, 2, "Winner: [%d] %s", game->winner_id,
              winner->name);
    unset_player_color(ctx.map_win, game->winner_id);

    if (game->winner_id == game->my_player_id) {
      wattron(ctx.map_win, A_BOLD | COLOR_PAIR(14));
      mvwprintw(ctx.map_win, 4, 2, "*** YOU WON! ***");
      wattroff(ctx.map_win, A_BOLD | COLOR_PAIR(14));
    }
  } else {
    mvwprintw(ctx.map_win, 3, 2, "Result: DRAW");
  }

  // Statistics table
  wattron(ctx.map_win, A_BOLD);
  mvwprintw(ctx.map_win, 6, 2, "%-20s %7s", "Player", "Bonuses");
  wattroff(ctx.map_win, A_BOLD);

  int line = 7;
  for (int player_id = 0; player_id < MAX_PLAYERS; player_id++) {
    const player_t *p = &game->players[player_id];
    if (!p->is_connected)
      continue;

    set_player_color(ctx.map_win, player_id);
    mvwprintw(ctx.map_win, line, 2, "[%d] %-16s %7u", player_id, p->name,
              game->bonuses_collected[player_id]);
    unset_player_color(ctx.map_win, player_id);
    line++;
  }

  mvwprintw(ctx.map_win, line + 2, 2, "Press [R] to return to lobby");
  mvwprintw(ctx.map_win, line + 3, 2, "Press [Q] to quit");

  wnoutrefresh(stdscr);
  wnoutrefresh(ctx.map_win);
  doupdate();
}

static void draw_gameplay_screen(const GameState *game) {

  if (ctx.needs_reinit || is_term_resized(ctx.screen_h, ctx.screen_w)) {
    recalculate_dimensions();
    erase();
  }

  werase(ctx.map_win);
  box(ctx.map_win, 0, 0);

  // spectator mode check
  const player_t *me = &game->players[game->my_player_id];
  const player_t *cam_target = me;
  bool spectating = false;

  if (!me->alive && game->spectate_target < MAX_PLAYERS &&
      game->players[game->spectate_target].alive) {
    cam_target = &game->players[game->spectate_target];
    spectating = true;
  }

  int cam_y = cam_target->row - (ctx.view_h / 2);
  int cam_x = cam_target->col - (ctx.view_w / 2);

  if (cam_x < 0)
    cam_x = 0;
  if (cam_y < 0)
    cam_y = 0;
  if (cam_y > game->map_height - ctx.view_h)
    cam_y = game->map_height - ctx.view_h;
  if (cam_x > game->map_width - ctx.view_w)
    cam_x = game->map_width - ctx.view_w;

  if (game->map_height < ctx.view_h)
    cam_y = 0;
  if (game->map_width < ctx.view_w)
    cam_x = 0;

  // Drawing map loop
  for (int y = 0; y < ctx.view_h; y++) {
    for (int x = 0; x < ctx.view_w; x++) {
      int world_y = y + cam_y;
      int world_x = x + cam_x;

      if (world_y < game->map_height && world_x < game->map_width) {
        uint8_t tile = game->map[world_y * game->map_width + world_x];

        int color = 0;
        switch (tile) {
        case '*':
          color = 10;
          break; // explosion
        case 'B':
          color = 11;
          break; // bomb
        case 'H':
          color = 12;
          break; // hard wall
        case 'S':
          color = 13;
          break; // soft wall
        case 'A':
        case 'R':
        case 'T':
        case 'N':
          color = 14;
          break; // bonuses
        }

        if (color) {
          wattron(ctx.map_win, COLOR_PAIR(color));
          mvwaddch(ctx.map_win, y + 1, x + 1, tile);
          wattroff(ctx.map_win, COLOR_PAIR(color));
        } else {
          mvwaddch(ctx.map_win, y + 1, x + 1, tile);
        }
      }
    }
  }

  // players

  for (int player_id = 0; player_id < MAX_PLAYERS; player_id++) {
    const player_t *p = &game->players[player_id];

    if (!p->alive) {
      continue;
    }
    if (p->row >= cam_y && p->row <= cam_y + ctx.view_h && p->col >= cam_x &&
        p->col <= cam_x + ctx.view_w) {
      int rel_x = (p->col - cam_x) + 1;
      int rel_y = (p->row - cam_y) + 1;

      set_player_color(ctx.map_win, player_id);
      mvwaddch(ctx.map_win, rel_y, rel_x, player_symbols[player_id]);
      unset_player_color(ctx.map_win, player_id);
    }
  }

  // arrows

  wattron(ctx.map_win, A_REVERSE | A_BOLD);
  if (cam_y > 0)
    mvwaddch(ctx.map_win, 0, (ctx.view_w / 2) + 1, ACS_UARROW);
  if (cam_y + ctx.view_h < game->map_height)
    mvwaddch(ctx.map_win, ctx.view_h + 1, (ctx.view_w / 2) + 1, ACS_DARROW);
  if (cam_x > 0)
    mvwaddch(ctx.map_win, (ctx.view_h / 2) + 1, 0, ACS_LARROW);
  if (cam_x + ctx.view_w < game->map_width)
    mvwaddch(ctx.map_win, (ctx.view_h / 2) + 1, ctx.view_w + 1, ACS_RARROW);
  wattroff(ctx.map_win, A_REVERSE | A_BOLD);

  // arrows end
  int ui_y = (ctx.screen_h + ctx.view_h + 2) / 2;

  // Clear the UI lines first so numbers don't overlap when shrinking
  move(ui_y, 0);
  clrtoeol();
  move(ui_y + 1, 0);
  clrtoeol();
  move(ui_y + 2, 0);
  clrtoeol();
  move(ui_y + 3, 0);
  clrtoeol();

  if (spectating) {
    // Spectator banner inside the map window
    wattron(ctx.map_win, A_BOLD | A_REVERSE);
    mvwprintw(ctx.map_win, 1, 2, " SPECTATING: %.20s [%d] ", cam_target->name,
              game->spectate_target);
    wattroff(ctx.map_win, A_BOLD | A_REVERSE);

    // Controls
    mvprintw(ui_y, (ctx.screen_w / 2) - 18, "A/D: Prev/Next Player  Q: Quit");
    mvprintw(ui_y + 1, (ctx.screen_w / 2) - 18, "YOU ARE DEAD");
  } else {
    // Normal controls
    mvprintw(ui_y, (ctx.screen_w / 2) - 15, "WASD: Move  Space: Bomb  Q: Quit");
  }

  // Print Player Stats
  // TODO : Maybe have here later live tracker?

  mvprintw(ui_y + 2, (ctx.screen_w / 2) - 25,
           "Bombs: %u | Radius: %u | Speed: %u | Timer: %u",
           cam_target->bomb_count, cam_target->bomb_radius, cam_target->speed,
           cam_target->bomb_timer_ticks);

  // Print Debug Data
  mvprintw(ui_y + 3, (ctx.screen_w / 2) - 25, "Player[%d, %d]  Camera[%d, %d]",
           cam_target->col, cam_target->row, cam_x, cam_y);

  // 5. Draw UI Indicators and Refresh

  wnoutrefresh(stdscr);
  wnoutrefresh(ctx.map_win);
  doupdate();
}

void frontend_draw(const GameState *game) {
  switch (game->status) {
  case GAME_LOBBY:
    draw_lobby(game);
    break;
  case GAME_RUNNING:
    draw_gameplay_screen(game);
    break;
  case GAME_END:
    draw_game_end(game);
    break;
  }
}

void frontend_cleanup(void) {
  if (ctx.map_win != NULL) {
    delwin(ctx.map_win);
    ctx.map_win = NULL;
  }

  if (!isendwin()) {
    endwin();
  }

  printf("Bomberman Client closed safely.\n");
}

GameAction frontend_handle_input(void) {
  GameAction action = ACTION_NONE;

  int c;
  while ((c = getch()) != ERR) {
    switch (c) {

    case 'w':
      action = ACTION_UP;
      break;
    case 's':
      action = ACTION_DOWN;
      break;
    case 'a':
      action = ACTION_LEFT;
      break;
    case 'd':
      action = ACTION_RIGHT;
      break;
    case ' ':
      action = ACTION_BOMB;
      break;
    case 'r':
      action = ACTION_READY;
      break;
    case 'q':
      return ACTION_QUIT;
    }
  }
  return action;
}

#include <time.h>
// Requires <time.h>
void frontend_sleep_until_next_frame(void) {
  static long last_time = 0;
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  long now = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;

  long elapsed = now - last_time;
  long wait = 50 - elapsed;

  if (wait > 0) {
    napms((int)wait);
  }

  clock_gettime(CLOCK_MONOTONIC, &ts);
  last_time = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
