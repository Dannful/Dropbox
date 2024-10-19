#include "../include/connection.h"
#include "../include/fs_sync.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

void deallocate() {
  close_connection();
  destroy();
}

int main(int argc, char *argv[]) {
  atexit(deallocate);
  switch (server_connect(argv[2], atoi(argv[3]))) {
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
  send_message();
  switch (initialize("./syncdir")) {
  case FILE_DESCRIPTOR_CREATE_ERROR:
    printf("Failed to initialize socket file descriptor!");
    return 1;
  case FAILED_TO_WATCH:
    printf("Failed to initialize watching of directory!");
  case OK:
    printf("Successfully created sync watcher!\n");
    break;
  }
  char *line = NULL;
  size_t size;
  while (getline(&line, &size, stdin) > 0) {
    send_message((u_int8_t *)line, size);
  }
  return 0;
}
