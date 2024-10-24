#include "../include/connection.h"
#include <asm-generic/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define PENDING_CONNECTIONS_BUFFER 5

typedef struct {
  struct sockaddr_in address;
  int file_descriptor;
  socklen_t length;
} client;

client server_client;

ConnectionResult server_listen(u_int16_t port) {
  pthread_t listen_thread;

  server_client.length = sizeof(server_client.address);
  server_client.file_descriptor = socket(AF_INET, SOCK_STREAM, 0);
  int optval = 1;
  setsockopt(server_client.file_descriptor, SOL_SOCKET, SO_REUSEADDR, &optval,
             sizeof(optval));

  if (server_client.file_descriptor == -1) {
    return SERVER_SOCKET_CREATION_FAILURE;
  }
  bzero(&server_client.address, sizeof(server_client.address));

  server_client.address.sin_family = AF_INET;
  server_client.address.sin_addr.s_addr = INADDR_ANY;
  server_client.address.sin_port = htons(port);

  if ((bind(server_client.file_descriptor,
            (struct sockaddr *)&server_client.address,
            sizeof(server_client.address))) < 0) {
    return SERVER_SOCKET_BIND_FAILURE;
  }

  if ((listen(server_client.file_descriptor, PENDING_CONNECTIONS_BUFFER)) < 0) {
    return SERVER_SOCKET_LISTEN_FAILURE;
  }

  printf("Server bound with socket %d. Spawning LISTEN thread...\n",
         server_client.file_descriptor);

  pthread_create(&listen_thread, NULL, thread_listen, NULL);
  pthread_join(listen_thread, NULL);

  return SERVER_CONNECTION_SUCCESS;
}

void *thread_listen(void *arg) {
  printf("Listening for connections...\n");
  while (1) {
    pthread_t thread;
    int *client_socket = malloc(sizeof(int));
    if ((*client_socket = accept(server_client.file_descriptor,
                                 (struct sockaddr *)&server_client.address,
                                 &server_client.length)) < 0) {
      free(client_socket);
      pthread_exit((void *)1);
    }
    printf("Received client connection request: %d. Spawning thread...\n",
           *client_socket);
    pthread_create(&thread, NULL, handle_client_connection, client_socket);
  }
  pthread_exit(0);
}

unsigned long hash(char str[], unsigned int size) {
  unsigned long hash = 5381;
  int c;

  while ((c = *str++))
    hash = ((hash << 5) + hash) + c;

  return hash % size;
}

ssize_t safe_recv(int socket, void *buffer, size_t amount, int flags) {
  ssize_t read = 0;
  while (read < amount) {
    if ((read += recv(socket, buffer + read, amount - read, flags)) == 0)
      return 0;
  }
  return amount;
}

void *handle_client_connection(void *arg) {
  int *client_connection_pointer = ((int *)arg);
  int client_connection = *client_connection_pointer;
  free(client_connection_pointer);

  int error = 0;
  socklen_t length = sizeof(error);
  unsigned int read_bytes = 0;
  unsigned int head = 0;
  FILE *path_descriptors[1024] = {0};
  while (1) {
    Packet packet;
    struct stat st = {0};

    printf("Waiting to read bytes from socket %d...\n", client_connection);

    if (safe_recv(client_connection, &packet, sizeof(packet), 0) == 0)
      break;
    printf("Received message of type %d from connection %d and %d bytes.\n",
           packet.type, client_connection, packet.length);
    uint8_t buffer[packet.length];
    safe_recv(client_connection, buffer, packet.length, 0);
    unsigned long username_length = strlen((char *)buffer) + 1;
    char username[username_length];
    strcpy(username, (char *)buffer);

    switch (packet.type) {
    case COMMAND: {
      unsigned long command_length =
          strlen((char *)(buffer + username_length)) + 1;
      char full_command[command_length];
      strcpy(full_command, (char *)(buffer + username_length));

      char command[4] = {};
      command[0] = full_command[0];
      command[1] = full_command[1];
      command[2] = full_command[2];
      command[3] = '\0';

      if (strcmp(command, "DEL") == 0) {
        unsigned long path_length =
            username_length + 3 + strlen(full_command + 4);
        char new_file_path[path_length];
        sprintf(new_file_path, "./%s/%s", username, full_command + 4);
        remove(new_file_path);
      } else if (strcmp(command, "SYN") == 0) {
        if (stat(username, &st) == -1) {
          mkdir(username, 0700);
        }
      }
      break;
    }
    case DATA: {
      unsigned long path_size = strlen((char *)(buffer + username_length)) + 1;
      char path[path_size];
      strcpy(path, (char *)(buffer + username_length));
      unsigned long hashed_index = hash(path, 1024);
      unsigned long total_path_size_part =
          path_size + username_length - 1 + 3 + 5;
      unsigned long total_path_size = path_size + username_length - 1 + 3;
      char path_with_folder_part[total_path_size_part];
      char path_with_folder[total_path_size];
      snprintf(path_with_folder_part, total_path_size_part, "./%s/%s.part",
               username, path);
      snprintf(path_with_folder, total_path_size, "./%s/%s", username, path);
      if (path_descriptors[hashed_index] == NULL) {
        path_descriptors[hashed_index] = fopen(path_with_folder_part, "wb");
      }

      FILE *file = path_descriptors[hashed_index];
      fwrite(buffer + username_length + path_size, sizeof(uint8_t),
             packet.length - username_length - path_size, file);
      if (packet.sequence_number == packet.total_size - 1) {
        remove(path_with_folder);
        rename(path_with_folder_part, path_with_folder);
        fclose(file);
        path_descriptors[hashed_index] = NULL;
      }
      break;
    }
    }
  }
  printf("Closing connection %d...\n", client_connection);
  pthread_exit(0);
}

void close_socket() { close(server_client.file_descriptor); }
