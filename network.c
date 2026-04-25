
#include "network.h"
#include "state.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <stdio.h>

int network_connect(const char *host, int port) {

  struct addrinfo hints, *res;

  char port_str[16];

  snprintf(port_str, sizeof(port_str), "%d", port);

  memset(&hints, 0, sizeof(hints));

  hints.ai_family = AF_UNSPEC;

  hints.ai_socktype = SOCK_STREAM;

  if (getaddrinfo(host, port_str, &hints, &res) != 0) {
    fprintf(stderr, "Network Error: DNS lookup failed for %s:%d\n", host, port);
    return -1;
  }

  int sock = -1;
  struct addrinfo *p;

  for (p = res; p != NULL; p = p->ai_next) {
    sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (sock < 0)
      continue;

    if (connect(sock, p->ai_addr, p->ai_addrlen) == 0) {
      break;
    }
    close(sock);
    sock = -1;
  }

  freeaddrinfo(res);

  if (sock < 0) {
    fprintf(stderr, "Network Error: Failed to connect to %s:%d\n", host, port);
    return -1;
  }

  int flags = fcntl(sock, F_GETFL, 0);
  fcntl(sock, F_SETFL, flags | O_NONBLOCK);

  return sock;
}

int network_close(int sock) {
  uint8_t packet[3] = {MSG_LEAVE, 0, 255};

  send(sock, packet, sizeof(packet), 0);

  shutdown(sock, SHUT_RDWR);
  return close(sock);
}

#define NET_BUFFER_SIZE 204800

static u_int8_t net_buf[NET_BUFFER_SIZE];

static int buffer_len = 0;

// buffer is place where we have our packet
// game is GameState
// sock here is only used for responding to PING packet
int process_single_packet(uint8_t *buffer, GameState *game, int sock) {

  msg_type_t msg_type = buffer[0];

  int payload = 3;

  switch (msg_type) {
  case MSG_ERROR: {
    // For now just ignore error messages from server
    // TODO: display error in chat/log
  } break;
  case MSG_SET_READY: {
    uint8_t id = buffer[1]; // sender_id
    if (id < MAX_PLAYERS) {
      game->players[id].ready = true;
    }
  } break;
  case MSG_WELCOME: {
    game->my_player_id = buffer[2]; // target_id = our assigned ID
    game->is_initialized = true;

    memcpy(game->server_name, &buffer[payload], MAX_SERVER_NAME);
    game->status = buffer[payload + 20];
    uint8_t lenght = buffer[payload + 21];

    if (lenght > MAX_PLAYERS) {
      game->status = 0;

      // LOG INVALID PACKET
      return -1;
    }

    int client_offset = payload + 22;
    for (int k = 0; k < lenght; k++) {
      uint8_t id = buffer[client_offset];
      if (id < MAX_PLAYERS) {
        game->players[id].is_connected = true;
        game->players[id].ready = buffer[client_offset + 1];
        memcpy(game->players[id].name, &buffer[client_offset + 2],
               MAX_PLAYER_NAME);
      }
      client_offset += 32; // Each client struct is 32 bytes
    }
  } break;
  case MSG_LEAVE: {
    uint8_t id = buffer[1];

    if (id < MAX_PLAYERS) { // maybe we ignore some invalid packets and just log
                            // them for now
      game->players[id].is_connected = false;
      game->players[id].alive = false;
    }

  } break;
  case MSG_DISCONNECT:
    break; // case MSG_DISCONNECT it is handeled outside of
           // this function
  case MSG_PING: {
    uint8_t packet[3];

    packet[0] = MSG_PONG;
    if (game->is_initialized) {
      packet[1] = game->my_player_id;
    } else {
      packet[1] = 0;
    }
    packet[2] = 255;

    send(sock, packet, sizeof(packet), 0);
  } break; // it is handled outside of this function
  case MSG_PONG:
    break; // rightnow do nothing
  case MSG_SET_STATUS: {
    game->status = buffer[payload];
  } break;
  case MSG_WINNER: {
    game->winner_id = buffer[payload];
  } break;
  case MSG_MOVED: {
    uint8_t player_id = buffer[payload];

    uint16_t location_net;

    memcpy(&location_net, &buffer[payload + 1], 2);

    uint16_t location = ntohs(location_net);
    if (player_id >= MAX_PLAYERS) {
      // log invalid player packet
      return -1;
    }
    if (location >= (uint16_t)(game->map_width * game->map_height)) {
      // log out of map packet
      return -1;
    }
    uint16_t row = location / game->map_width;
    uint16_t col = location % game->map_width;
    game->players[player_id].col = col;
    game->players[player_id].row = row;
  } break;
  case MSG_DEATH: {
    uint8_t player_id = buffer[payload];

    if (player_id >= MAX_PLAYERS) {
      // log invalid player packet
      return -1;
    }
    // log player death in chat
    game->players[player_id].alive = false;
  } break;
  case MSG_MAP: {
    uint8_t h = buffer[payload];
    uint8_t w = buffer[payload + 1];
    if (h * w > MAX_GRID_SIZE * MAX_GRID_SIZE) {
      // log invalid map packet invalid size of {h * w}
      return -1;
    }
    game->map_width = w;
    game->map_height = h;
    memcpy(game->map, &buffer[payload + 2], h * w);
  } break;
  case MSG_BOMB: {
    // not sure about this one right now i think we setup at the cell B as to
    // bomb
    (void)buffer[payload]; // owner id — unused for now
    uint16_t net_location;
    memcpy(&net_location, &buffer[payload + 1], sizeof(net_location));
    uint16_t location = ntohs(net_location);
    if (location >= (uint16_t)(game->map_width * game->map_height)) {
      // LOG Out of map location
      return -1;
    }
    game->map[location] = 'B';
  } break;
  case MSG_EXPLOSION_START: {
    uint8_t radius = buffer[payload];
    uint16_t location;
    memcpy(&location, &buffer[payload + 1], 2);
    location = ntohs(location);

    if (location >= game->map_width * game->map_height)
      return -1;

    int center_r = location / game->map_width;
    int center_c = location % game->map_width;

    int dr[] = {-1, 1, 0, 0}; // Up, Down, Left, Right
    int dc[] = {0, 0, -1, 1};

    game->map[location] = '*'; // Center of explosion

    for (int dir = 0; dir < 4; dir++) {
      for (int step = 1; step <= radius; step++) {
        int r = center_r + (dr[dir] * step);
        int c = center_c + (dc[dir] * step);

        if (r < 0 || r >= game->map_height || c < 0 || c >= game->map_width)
          break;

        int idx = r * game->map_width + c;
        uint8_t cell = game->map[idx];

        if (cell == 'H' || cell == 'S')
          break; // Hard wall and soft block both stop explosion

        game->map[idx] = '*'; // Mark as explosion
      }
    }
  } break;
  case MSG_EXPLOSION_END: {
    uint8_t radius = buffer[payload];
    uint16_t location;
    memcpy(&location, &buffer[payload + 1], 2);
    location = ntohs(location);

    if (location >= game->map_width * game->map_height)
      return -1;

    int center_r = location / game->map_width;
    int center_c = location % game->map_width;

    int dr[] = {-1, 1, 0, 0};
    int dc[] = {0, 0, -1, 1};

    // Clear center
    if (game->map[location] == '*')
      game->map[location] = '.';

    for (int dir = 0; dir < 4; dir++) {
      for (int step = 1; step <= radius; step++) {
        int r = center_r + (dr[dir] * step);
        int c = center_c + (dc[dir] * step);

        if (r < 0 || r >= game->map_height || c < 0 || c >= game->map_width)
          break;

        int idx = r * game->map_width + c;
        if (game->map[idx] == 'H')
          break;
        if (game->map[idx] == '*')
          game->map[idx] = '.';
        if (game->map[idx] == 'S')
          break; // Was a soft block wall — stop
      }
    }
  } break;
  case MSG_BONUS_AVAILABLE: {
    uint8_t bonus = buffer[payload];

    uint16_t net_location;
    memcpy(&net_location, &buffer[payload + 1], sizeof(net_location));
    uint16_t location = ntohs(net_location);

    if (location >= (uint16_t)(game->map_width * game->map_height)) {
      // log location out of map
      return -1;
    }
    // Map bonus enum to map character: 1='A', 2='R', 3='T', 4='N'
    char bonus_chars[] = {'.', 'A', 'R', 'T', 'N'};
    if (bonus > 0 && bonus < sizeof(bonus_chars))
      game->map[location] = (uint8_t)bonus_chars[bonus];
    else
      game->map[location] = '?';
  } break;
  case MSG_BONUS_RETRIEVED: {
    (void)buffer[payload]; // player id — unused for now

    uint16_t net_location;
    memcpy(&net_location, &buffer[payload + 1], sizeof(net_location));
    uint16_t location = ntohs(net_location);

    if (location >= (uint16_t)(game->map_width * game->map_height)) {
      // log location out of map
      return -1;
    }
    // check if player id is valid? or ignore
    game->map[location] = '.';
  } break;
  case MSG_BLOCK_DESTROYED: {

    uint16_t net_location;
    memcpy(&net_location, &buffer[payload], sizeof(net_location));
    uint16_t location = ntohs(net_location);

    if (location >= (uint16_t)(game->map_width * game->map_height)) {
      // log location out of map
      return -1;
    }
    game->map[location] = '.';
  } break;
  default:
    return -1;
  }
  return 0;
}

NetworkEvent handle_network_updates(int sock, GameState *game) {

  ssize_t bytes_read =
      recv(sock, net_buf + buffer_len, sizeof(net_buf) - (size_t)buffer_len, 0);
  if (bytes_read > 0) {
    buffer_len += (int)bytes_read;
  } else if (bytes_read == 0)
    return NETWORK_DISCONNECT;

  int i = 0;

  while (i < buffer_len) {
    if (buffer_len - i < 3)
      break;

    msg_type_t msg_type = net_buf[i];

    int packet_size = 0;

    int payload = i + 3;
    if (msg_type == MSG_WELCOME) {
      if (buffer_len - i < 25)
        break;
      uint8_t lenght = net_buf[payload + 21];
      packet_size = 25 + 32 * lenght;
    } else if (msg_type == MSG_LEAVE)
      packet_size = 3;
    else if (msg_type == MSG_DISCONNECT)
      return NETWORK_DISCONNECT; // here maybe we clean buffer and buff_len??
    else if (msg_type == MSG_PING)
      packet_size = 3;
    else if (msg_type == MSG_PONG)
      packet_size = 3;
    else if (msg_type == MSG_ERROR) {
      // Variable length: header(3) + uint16_t len + string
      // For simplicity, we need at least 5 bytes to read len
      if (buffer_len - i < 5)
        break;
      uint16_t err_len;
      memcpy(&err_len, &net_buf[payload], 2);
      err_len = ntohs(err_len);
      packet_size = 5 + err_len;
    } else if (msg_type == MSG_SET_READY)
      packet_size = 3;
    else if (msg_type == MSG_SET_STATUS)
      packet_size = 4;
    else if (msg_type == MSG_WINNER)
      packet_size = 4;
    else if (msg_type == MSG_MOVED)
      packet_size = 6;
    else if (msg_type == MSG_DEATH)
      packet_size = 4;
    else if (msg_type == MSG_MAP) {
      if (buffer_len - payload < 2)
        break;

      uint8_t h = net_buf[payload];
      uint8_t w = net_buf[payload + 1];
      packet_size = 5 + h * w;
    } else if (msg_type == MSG_BOMB)
      packet_size = 6;
    else if (msg_type == MSG_EXPLOSION_START)
      packet_size = 6;
    else if (msg_type == MSG_EXPLOSION_END)
      packet_size = 6;
    else if (msg_type == MSG_BONUS_AVAILABLE)
      packet_size = 6;
    else if (msg_type == MSG_BONUS_RETRIEVED)
      packet_size = 6;
    else if (msg_type == MSG_BLOCK_DESTROYED)
      packet_size = 5;

    else {
      // LOG recieved unknown packed {PACKET ID}
      return NETWORK_ERROR;
    }

    if (buffer_len - i < packet_size)
      break;
    int result = process_single_packet(&net_buf[i], game, sock);

    if (result == -1) {
      return NETWORK_ERROR;
    }

    i += packet_size;
  }
  int leftovers = buffer_len - i;

  if (leftovers > 0) {
    memmove(net_buf, &net_buf[i], (size_t)leftovers);
  }
  buffer_len = leftovers;

  return NETWORK_OK;
}

void network_send_hello(int sock, const char *client_name,
                        const char *player_name) {
  uint8_t packet[53];

  packet[0] = MSG_HELLO;
  packet[1] = 0;
  packet[2] = 255;

  strncpy((char *)&packet[3], client_name, 20);

  strncpy((char *)&packet[23], player_name, 30);

  send(sock, packet, sizeof(packet), 0);
}

void network_send_move_attempt(int sock, uint8_t my_id, char direction) {
  uint8_t packet[4];

  packet[0] = MSG_MOVE_ATTEMPT;
  packet[1] = my_id;
  packet[2] = 255;

  packet[3] = (uint8_t)direction;

  send(sock, packet, sizeof(packet), 0);
}

void network_send_bomb_attempt(int sock, uint8_t my_id, uint16_t cell_index) {
  uint8_t packet[5];

  packet[0] = MSG_BOMB_ATTEMPT;
  packet[1] = my_id;
  packet[2] = 255;

  uint16_t net_cell_index = htons(cell_index);

  memcpy(&packet[3], &net_cell_index, 2);

  send(sock, packet, sizeof(packet), 0);
}

void network_send_ready(int sock, uint8_t my_id) {
  uint8_t packet[3];

  packet[0] = MSG_SET_READY;
  packet[1] = my_id;
  packet[2] = 255;

  send(sock, packet, sizeof(packet), 0);
}
