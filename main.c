#define STATE_IMPL

#include <ncurses.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "frontend.h"
#include "network.h"
#include "state.h"

#include "log.h"

#define CLIENT_NAME "PD_KD_Client/1.0"
#define DEFAULT_PORT 25565

static void print_usage(const char *prog) {
  printf("Bomberman Client\n");
  printf("Usage: %s <server_ip> <player_name> [port]\n", prog);
  printf("  hostname     - Server hostname or IP adress\n");
  printf("  player_name  - Your player name (max 29 chars)\n");
  printf("  port         - Server port (default: %d)\n", DEFAULT_PORT);
}

int main(int argc, char *argv[]) {
  if (argc < 3) {
    print_usage(argv[0]);
    return 1;
  }

  if (log_init() == -1) {
    fprintf(stderr, "Log initialization failed\n");
    return 1;
  }

  const char *server_ip = argv[1];
  const char *player_name = argv[2];
  int port = DEFAULT_PORT;

  if (argc >= 4) {
    port = atoi(argv[3]);
    if (port <= 0 || port > 65535) {
      fprintf(stderr, "Invalid port: %s\n", argv[3]);
      return 1;
    }
  }

  // Connect to server
  printf("Connecting to %s:%d...\n", server_ip, port);

  int sock = network_connect(server_ip, port);
  if (sock < 0) {
    fprintf(stderr, "Failed to connect to %s:%d\n", server_ip, port);
    return 1;
  }

  printf("Connected! Sending HELLO...\n");

  // Initialize game state
  GameState game;
  init_game_state(&game);

  // Send HELLO
  network_send_hello(sock, CLIENT_NAME, player_name);

  // Wait for WELCOME (blocking wait, up to 30 seconds)
  {
    struct pollfd pfd = {.fd = sock, .events = POLLIN};
    int timeout_ms = 30000;
    int waited = 0;

    while (!game.is_initialized && waited < timeout_ms) {
      int ret = poll(&pfd, 1, 100);
      if (ret > 0 && (pfd.revents & POLLIN)) {
        NetworkEvent ev = handle_network_updates(sock, &game);
        if (ev == NETWORK_DISCONNECT) {
          fprintf(stderr, "Server disconnected during handshake.\n");
          close(sock);
          return 1;
        }
        if (ev == NETWORK_ERROR) {
          fprintf(stderr, "Network error during handshake.\n");
          close(sock);
          return 1;
        }
      }
      waited += 100;
    }

    if (!game.is_initialized) {
      fprintf(stderr, "Timeout: no WELCOME received within 30 seconds.\n");
      close(sock);
      return 1;
    }
  }

  printf("WELCOME received! Player ID: %d, Status: %d\n", game.my_player_id,
         game.status);
  printf("Starting frontend...\n");

  // Short delay so the user can see the connection messages
  struct timespec delay = {.tv_sec = 0, .tv_nsec = 500000000};
  nanosleep(&delay, NULL);

  // Initialize ncurses frontend
  if (!frontend_init()) {
    fprintf(stderr, "Failed to initialize frontend (no color support?)\n");
    network_close(sock, game.my_player_id);
    return 1;
  }

  // Main game loop using poll() for multiplexing stdin + socket
  bool should_quit = false;

  // poll on two fds: stdin (for keyboard) and socket (for network)
  struct pollfd fds[2];
  fds[0].fd = STDIN_FILENO; // keyboard input (ncurses reads from stdin)
  fds[0].events = POLLIN;
  fds[1].fd = sock; // network socket
  fds[1].events = POLLIN;

  while (!should_quit) {

    // Poll with a timeout of ~50ms (20 ticks per second)
    int ret = poll(fds, 2, 50);

    // Handle keyboard input
    GameAction action = frontend_handle_input();

    switch (game.status) {
    case GAME_LOBBY: {
      // In lobby: only READY and QUIT are meaningful
      switch (action) {
      case ACTION_READY:
        network_send_ready(sock, game.my_player_id);
        break;
      case ACTION_QUIT:
        should_quit = true;
        break;
      default:
        break;
      }
    } break;

    case GAME_RUNNING: {
      player_t *me = &game.players[game.my_player_id];

      if (me->alive) {
        // Alive: normal gameplay controls
        switch (action) {
        case ACTION_UP:
          network_send_move_attempt(sock, game.my_player_id, 'U');
          break;
        case ACTION_DOWN:
          network_send_move_attempt(sock, game.my_player_id, 'D');
          break;
        case ACTION_LEFT:
          network_send_move_attempt(sock, game.my_player_id, 'L');
          break;
        case ACTION_RIGHT:
          network_send_move_attempt(sock, game.my_player_id, 'R');
          break;
        case ACTION_BOMB: {
          uint16_t cell = make_cell_index(me->row, me->col, game.map_width);
          network_send_bomb_attempt(sock, game.my_player_id, cell);
        } break;
        case ACTION_QUIT:
          should_quit = true;
          break;
        default:
          break;
        }
      } else {
        // Dead: spectator mode - cycle through alive players
        if (action == ACTION_QUIT) {
          should_quit = true;
        } else if (action == ACTION_RIGHT || action == ACTION_DOWN) {
          // Next alive player
          uint8_t start =
              game.spectate_target < MAX_PLAYERS ? game.spectate_target : 0;
          for (int k = 1; k <= MAX_PLAYERS; k++) {
            uint8_t idx = (start + k) % MAX_PLAYERS;
            if (game.players[idx].alive && game.players[idx].is_connected) {
              game.spectate_target = idx;
              break;
            }
          }
        } else if (action == ACTION_LEFT || action == ACTION_UP) {
          // Previous alive player
          uint8_t start =
              game.spectate_target < MAX_PLAYERS ? game.spectate_target : 0;
          for (int k = 1; k <= MAX_PLAYERS; k++) {
            uint8_t idx = (start - k + MAX_PLAYERS) % MAX_PLAYERS;
            if (game.players[idx].alive && game.players[idx].is_connected) {
              game.spectate_target = idx;
              break;
            }
          }
        }
      }
    } break;

    case GAME_END: {
      // In end screen: quit or go back to lobby
      switch (action) {
      case ACTION_READY:
        // Send SET_STATUS(0) to return to lobby
        network_send_set_status(sock, game.my_player_id, 0);
        break;
      case ACTION_QUIT:
        should_quit = true;
        break;
      default:
        break;
      }
    } break;
    }

    // Handle network updates
    if (ret > 0 && (fds[1].revents & POLLIN)) {
      NetworkEvent ev = handle_network_updates(sock, &game);
      if (ev == NETWORK_DISCONNECT) {
        should_quit = true;
      } else if (ev == NETWORK_ERROR) {
        should_quit = true;
      }
    }

    // Check for socket errors
    if (ret > 0 && (fds[1].revents & (POLLERR | POLLHUP))) {
      should_quit = true;
    }

    // Draw current state
    frontend_draw(&game);

    (void)ret; // suppress unused warning if poll returns 0 (timeout)
  }

  // Cleanup
  frontend_cleanup();
  network_close(sock, game.my_player_id);
  log_close();

  return 0;
}
