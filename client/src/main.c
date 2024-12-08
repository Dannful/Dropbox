#include "../../core/utils.h"
#include "../include/connection.h"
#include "../../core/connection.h"
#include "../include/fs_sync.h"
#include "signal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define REVERSE_CONNECTION_PORT 6666

extern Map *path_descriptors;
extern Map *file_timestamps;
extern Map *files_writing;

void deallocate() {
  close_connection();
  destroy();
  if (path_descriptors != NULL)
    hash_destroy(path_descriptors);
  if (file_timestamps != NULL)
    hash_destroy(file_timestamps);
  if (files_writing != NULL)
    hash_destroy(files_writing);
}

int main(int argc, char *argv[]) {
  atexit(deallocate);
  set_username(argv[1]);
  set_server_data(argv[2], atoi(argv[3]));

  switch (open_control_connection()) {
  case CONNECTION_INVALID_ADDRESS:
    printf("The supplied server address is invalid!\n");
    return 1;
  case CONNECTION_SOCKET_FAILURE:
    printf("Failed to create socket.\n");
    return 1;
  case CONNECT_FAILURE:
    printf("Failed to connect to server!\n");
    return 1;
  case SERVER_CONNECTION_SUCCESS:
    send_sync_dir_message();
    break;
  }
  printf("Setting up reverse connection server...\n");
  switch (setup_reverse_connection_listener(REVERSE_CONNECTION_PORT)){
    case SERVER_ACCEPT_FAILURE:
      printf("Server has failed to accept client connection!\n");
      return 1;
    case SERVER_SOCKET_CREATION_FAILURE:
      printf("Failed to create server control socket!\n");
      return 1;
    case SERVER_SOCKET_LISTEN_FAILURE:
      printf("Failed to set server control socket to LISTEN mode!\n");
      return 1;
    case SERVER_SOCKET_BIND_FAILURE:
      printf("Failed to bind server control socket! Make sure there is no other process running on port %d\n",
           REVERSE_CONNECTION_PORT);
      return 1;
    case SERVER_SUCCESS:
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
  signal(SIGPIPE, SIG_IGN);
  char input[1024] = {0};
  char argument[1024] = {0};
  while (1) {
    fgets(input, sizeof(input), stdin);
    char *command = strtok(input, " ");
    command[strcspn(command, "\n")] = 0;

    if (strcmp(command, "upload") == 0) {
      pthread_t upload;
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
      send_download_message(argument, 0);
    } else if (strcmp(command, "list_server") == 0) {
      send_list_server_message();
    } else if (strcmp(command, "list_client") == 0) {
      char file_list_string[4096] = {0};
      char sdyr_path[100] = "./syncdir";
      generate_file_list_string(sdyr_path, file_list_string, 4096);
      printf("%s", file_list_string);
    } else if (strcmp(command, "exit") == 0) {
      close_connection();
      close_reverse_connection();
      return 0;
    }
  }
  return 0;
}
