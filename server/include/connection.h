#ifndef CONNECTION_H
#define CONNECTION_H

#include <sys/types.h>
typedef enum {
  SERVER_ACCEPT_FAILURE = -4,
  SOCKET_CREATION_FAILURE = -3,
  SOCKET_LISTEN_FAILURE = -2,
  SOCKET_BIND_FAILURE = -1,
  SUCCESS = 0
} ConnectionResult;

ConnectionResult server_listen(u_int16_t port);
void send_message(u_int8_t bytes[], unsigned int count);
void *handle_client_connection(void *arg);
void *thread_listen(void *arg);
void close_socket();

#endif
