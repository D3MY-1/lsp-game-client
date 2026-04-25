#ifndef NETWORK_H
#define NETWORK_H

#include "frontend.h"
#include "state.h"
#include <stdint.h>

typedef enum {
  NETWORK_OK,
  NETWORK_DISCONNECT,
  NETWORK_ERROR,

} NetworkEvent;

int network_connect(const char *ip, int port);

int network_close(int sock);

NetworkEvent handle_network_updates(int sock, GameState *game);

void network_send_hello(int sock, const char *client_name,
                        const char *player_name);

void network_send_move_attempt(int socket_fd, uint8_t my_id, char direction);

void network_send_bomb_attempt(int sock, uint8_t my_id, uint16_t cell_index);

void network_send_ready(int sock, uint8_t my_id);

#endif
