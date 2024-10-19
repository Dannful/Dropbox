#ifndef CONNECTION_H
#define CONNECTION_H

#include <sys/types.h>
typedef enum {
  INVALID_ADDRESS = -3,
  SOCKET_FAILURE = -2,
  CONNECT_FAILURE = -1,
  SUCCESS = 0
} ConnectionResult;

typedef enum { COMMAND = 0, DATA = 1 } MessageType;

typedef struct {
  MessageType type;
  u_int16_t sequence_number;
  u_int32_t fragment_count;
  u_int16_t data_length;
  const u_int8_t *payload;
} Packet;

ConnectionResult server_connect(char host[], u_int16_t port);
void send_message(Packet packet);
void send_upload_message(char path[]);
void send_download_message(char path[]);
void send_delete_message(char path[]);
char *send_list_server_message();
char *send_list_client_message();
void send_sync_dir_message();
void close_connection();

#endif
