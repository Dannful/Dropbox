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
  case CONNECTION_INVALID_ADDRESS:
    printf("The supplied server address is invalid!\n");
    return 1;
  case CONNECTION_SOCKET_FAILRUE:
    printf("Failed to create socket.\n");
    return 1;
  case CONNECT_FAILURE:
    printf("Failed to connect to server!\n");
    return 1;
  case SERVER_CONNECTION_SUCCESS:
    break;
  }
  switch (initialize("./syncdir")) {
  case FILE_DESCRIPTOR_CREATE_ERROR:
    printf("Failed to initialize socket file descriptor!\n");
    return 1;
  case FAILED_TO_WATCH:
    printf("Failed to initialize watching of directory!\n");
    break;
  case OK:
    printf("Successfully created sync watcher!\n");
    break;
  }
  send_upload_message(
      "/home/dannly/Documents/CS/Personal/C/Dropbox/romano.txt");
  return 0;
}
