#include "../include/connection.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PORT 8080

int main(void) {
  atexit(close_socket);
  switch (server_listen(PORT)) {
  case SERVER_ACCEPT_FAILURE:
    printf("Server has failed to accept client connection!\n");
    return 1;
  case SERVER_SOCKET_CREATION_FAILURE:
    printf("Failed to create server listen socket!\n");
    return 1;
  case SERVER_SOCKET_LISTEN_FAILURE:
    printf("Failed to set server socket to LISTEN mode!\n");
    return 1;
  case SERVER_SOCKET_BIND_FAILURE:
    printf("Failed to bind server socket! Make sure there is no other process "
           "running on port %d\n",
           PORT);
    return 1;
  case SERVER_CONNECTION_SUCCESS:
    break;
  }

  while (1) {
    char input[1024] = {0};
    char argument[1024] = {0};
    while (1) {
      fgets(input, sizeof(input), stdin);
      printf("%s\n", input);
      if (strcmp(input, "exit") == 0) {
        close_socket();
        exit(0);
        return 0;
      }
    }
  }

  return 0;
}
