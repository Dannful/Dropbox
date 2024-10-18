#ifndef CONNECTION_H
#define CONNECTION_H

#include <sys/types.h>

typedef enum {
  INVALID_ADDRESS = -3,
  SOCKET_FAILURE = -2,
  CONNECT_FAILURE = -1,
  SUCCESS = 0
} ConnectionResult;

ConnectionResult server_connect(char host[], u_int16_t port);
void send_message(u_int8_t bytes[], unsigned int count);
void close_connection();

#endif
