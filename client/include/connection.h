#ifndef CONNECTION_H
#define CONNECTION_H

#include "../../core/packet.h"
#include <stdint.h>

typedef enum {
  CONNECTION_INVALID_ADDRESS = -3,
  CONNECTION_SOCKET_FAILRUE = -2,
  CONNECT_FAILURE = -1,
  SERVER_CONNECTION_SUCCESS = 0
} ConnectionResult;

ConnectionResult server_connect(char host[], uint16_t port);
void send_upload_message(char path[]);
void send_download_message(char path[]);
void send_delete_message(char path[]);
char *send_list_server_message();
char *send_list_client_message();
void send_sync_dir_message();
void close_connection();
void set_username(char user[USERNAME_LENGTH]);

#endif
