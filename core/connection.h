#ifndef CORE_CONNECTION_H
#define CORE_CONNECTION_H

#include <stdint.h>

typedef enum {
  CONNECTION_INVALID_ADDRESS = -3,
  CONNECTION_SOCKET_FAILURE = -2,
  CONNECT_FAILURE = -1,
  SERVER_CONNECTION_SUCCESS = 0
} ConnectionResult;

ConnectionResult open_connection(int *fd, char *host, uint16_t port);

#endif
