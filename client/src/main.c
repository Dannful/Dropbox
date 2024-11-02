#include "../include/connection.h"
#include "../include/fs_sync.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

extern Map *path_descriptors;

void deallocate() {
  close_connection();
  destroy();
  if (path_descriptors != NULL)
    hash_destroy(path_descriptors);
}

int main(int argc, char *argv[]) {
  atexit(deallocate);
  set_username(argv[1]);
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
    send_sync_dir_message();
    break;
  }
  struct stat st = {0};
  if (stat("./syncdir", &st) == -1) {
    mkdir("./syncdir", 0700);
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
  char input[1024] = {0};
  char argument[1024] = {0};
  while (1) {
    fgets(input, sizeof(input), stdin);
    char *command = strtok(input, " ");
    command[strcspn(command, "\n")] = 0;

    if (strcmp(command, "upload") == 0) {
      char *argument = strtok(NULL, " ");
      argument[strlen(argument) - 1] = '\0';
      send_upload_message(argument);
    } else if (strcmp(command, "get_sync_dir") == 0) {
      send_sync_dir_message();
    } else if (strcmp(command, "delete") == 0) {
      char *argument = strtok(NULL, " ");
      argument[strlen(argument) - 1] = '\0';
      send_delete_message(argument);
    } else if (strcmp(command, "download") == 0) {
      char *argument = strtok(NULL, " ");
      argument[strlen(argument) - 1] = '\0';
      send_download_message(argument);
    } else if (strcmp(command, "list_server") == 0) {
      send_list_server_message();
    } else if (strcmp(command, "exit") == 0) {
      close_connection();
      return 0;
    }
  }
  return 0;
}
