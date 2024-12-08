#include "../include/connection.h"
#include "../../core/connection.h"
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/election.h"

int main(int argc, char *argv[]) {
  if (argc == 1) {
    printf("Missing replica identifier.\n");
    return 1;
  }
  if (argc == 2) {
    printf("Missing number of server replicas.\n");
    return 1;
  }
  if(argc == 3) {
    printf("Missing primary server identifier.\n");
    return 1;
  }
  if(argc == 4) {
    printf("Missing control socket port.\n");
    return 1;
  }
  if(argc == 5) {
    printf("Missing cluster socket port.\n");
    return 1;
  }
  set_number_of_replicas(atoi(argv[2]));
  uint8_t replica_id = atoi(argv[1]);
  if (replica_id >= get_number_of_replicas()) {
    printf("Replica identifier must be within [0, %d).\n",
           get_number_of_replicas());
    return 1;
  }
  set_replica_id(replica_id);
  set_primary_server(atoi(argv[3]));

  set_control_port(atoi(argv[4]));
  set_cluster_port(atoi(argv[5]));

  extern Map *path_descriptors;
  path_descriptors = hash_create();

  read_server_data_file();
  atexit(deallocate);

  switch (create_control_socket(get_control_port())) {
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
    printf("Failed to bind server control socket! Make sure there is no other process "
           "running on port %d\n",
           get_control_port());
    return 1;
  case SERVER_SUCCESS:
    break;
  }

  switch (create_cluster_socket(get_cluster_port())) {
  case SERVER_ACCEPT_FAILURE:
    printf("Server has failed to accept client connection!\n");
    return 1;
  case SERVER_SOCKET_CREATION_FAILURE:
    printf("Failed to create server cluster socket!\n");
    return 1;
  case SERVER_SOCKET_LISTEN_FAILURE:
    printf("Failed to set server cluster socket to LISTEN mode!\n");
    return 1;
  case SERVER_SOCKET_BIND_FAILURE:
    printf("Failed to bind server cluster socket! Make sure there is no other process "
           "running on port %d\n",
           get_cluster_port());
    return 1;
  case SERVER_SUCCESS:
    break;
  }

  signal(SIGPIPE, SIG_IGN);

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
