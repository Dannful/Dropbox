#include "../include/connection.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  atexit(close_connection);
  switch (server_connect("127.0.0.1", 1234)) {
  case INVALID_ADDRESS:
    printf("The supplied server address is invalid!");
    return 1;
  case SOCKET_FAILURE:
    printf("Failed to create socket.");
    return 1;
  case CONNECT_FAILURE:
    printf("Failed to connect to server!");
    return 1;
  case SUCCESS:
    break;
  }
  char *line = NULL;
  size_t size;
  while (getline(&line, &size, stdin) > 0) {
    send_message((u_int8_t *)line, size);
  }
  return 0;
}
