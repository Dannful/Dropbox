#include "../include/connection.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/election.h"

int main(int argc, char *argv[]) {
  if (argc == 1) {
    printf("Missing number of server replicas.\n");
    return 1;
  }
  if (argc == 2) {
    printf("Missing replica identifier.\n");
    return 1;
  }
  if(argc == 3) {
    printf("Missing primary server identifier.\n");
    return 1;
  }
  set_number_of_replicas(atoi(argv[1]));
  uint8_t replica_id = atoi(argv[2]);
  if (replica_id >= get_number_of_replicas()) {
    printf("Replica identifier must be within [0, %d).\n",
           get_number_of_replicas());
    return 1;
  }
  set_replica_id(replica_id);
  set_primary_server(atoi(argv[3]));
  atexit(deallocate);
  switch (server_listen(BASE_PORT + replica_id)) {
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
           BASE_PORT + replica_id);
    return 1;
  case SERVER_SUCCESS:
    break;
  }

  while (1) {
    char input[1024] = {0};
    char argument[1024] = {0};
    while (1) {
      fgets(input, sizeof(input), stdin);
      char *command = strtok(input, " ");
      command[strcspn(command, "\n")] = 0;
      if (strcmp(input, "exit") == 0) {
        exit(0);
        return 0;
      }
    }
  }

  return 0;
}
