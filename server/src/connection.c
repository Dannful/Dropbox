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
    MessageType type;
    CommandPacket command_packet;
    DataPacket data_packet;

    printf("Waiting to read bytes from socket %d...\n", client_connection);
    if (recv(client_connection, &type, sizeof(MessageType), 0) == 0)
      break;
    printf("Received message of type %d.\n", type);
    switch (type) {
    case COMMAND:
      recv(client_connection, &command_packet, sizeof(CommandPacket), 0);
      break;
    case DATA:
      recv(client_connection, &data_packet,
           sizeof(data_packet) - sizeof(data_packet.data), 0);
      printf("Received %d bytes from a data packet. Current sequence is %d out "
             "of %d "
             "fragments. Writing to path "
             "%s...\n",
             data_packet.data_length, data_packet.sequence_number,
             data_packet.fragment_count, data_packet.path);
      unsigned int index = hash(data_packet.path, 1024);
      uint8_t data[data_packet.data_length];
      if (path_descriptors[index] == NULL) {
        path_descriptors[index] = fopen(data_packet.path, "wb");
      }
      FILE *file = path_descriptors[index];
      recv(client_connection, data, data_packet.data_length, 0);
      fwrite(data, sizeof(uint8_t), data_packet.data_length, file);
      if (data_packet.sequence_number == data_packet.fragment_count - 1) {
        printf("Closing file %s...\n", data_packet.path);
        fclose(file);
        path_descriptors[index] = NULL;
      }
      break;
    }
  }
  printf("Closing connection %d...\n", client_connection);
  pthread_exit(0);
}

void close_socket() { close(server_client.file_descriptor); }
