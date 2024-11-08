#include "../include/connection.h"
#include "../../core/utils.h"
#include "../../core/writer.h"
#include "math.h"
#include <asm-generic/socket.h>
#include <dirent.h>
#include <libgen.h>
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
#include <utime.h>

#define PENDING_CONNECTIONS_BUFFER 5

typedef struct {
  struct sockaddr_in address;
  int file_descriptor;
  socklen_t length;
} client;

client server_client;
Map *path_descriptors;

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

  path_descriptors = hash_create();

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
  hash_destroy(path_descriptors);
  pthread_exit(0);
}

void *handle_client_connection(void *arg) {
  int *client_connection_pointer = ((int *)arg);
  int client_connection = *client_connection_pointer;
  free(client_connection_pointer);

  int error = 0;
  socklen_t length = sizeof(error);
  unsigned int read_bytes = 0;
  while (1) {
    Packet packet;
    struct stat st = {0};

    printf("Waiting to read bytes from socket %d...\n", client_connection);

    if (safe_recv(client_connection, &packet, sizeof(packet), 0) == 0)
      break;
    printf("Received message of type %d from connection %d and %d bytes.\n",
           packet.type, client_connection, packet.length);
    uint8_t buffer[packet.length];
    if (safe_recv(client_connection, buffer, packet.length, 0) == 0)
      break;
    Reader *reader = create_reader(buffer);
    char *username = read_string(reader);
    unsigned long username_length = strlen(username);

    switch (packet.type) {
    case COMMAND: {
      CommandType command = read_ulong(reader);

      switch (command) {
      case COMMAND_DOWNLOAD: {
        char *arguments = read_string(reader);
        unsigned long command_path_length = strlen(arguments);
        unsigned long path_length = username_length + 4 + command_path_length;
        char new_file_path[path_length];
        sprintf(new_file_path, "./%s/%s", username, arguments);
        printf("Received download request from user %s and file %s.\n",
               username, new_file_path);
        send_file(new_file_path, arguments, username, client_connection);
        free(arguments);
        break;
      }
      case COMMAND_DELETE: {
        char *arguments = read_string(reader);
        unsigned long command_path_length = strlen(arguments);
        unsigned long path_length = username_length + 4 + command_path_length;
        char new_file_path[path_length];
        sprintf(new_file_path, "./%s/%s", username, arguments);
        printf("Received DELETE request from user %s and path %s.\n", username,
               arguments);
        if (remove(new_file_path) != 0)
          break;
        send_delete_message(client_connection, arguments);
        free(arguments);
        break;
      }
      case COMMAND_LIST: {
        printf("Received LST request from user %s.\n", username);
        send_list_response(username, client_connection);
        break;
      }
      case COMMAND_SYNC_DIR: {
        printf("Received SYN request from user %s.\n", username);
        if (stat(username, &st) == -1) {
          mkdir(username, 0700);
        }
        break;
      }
      case COMMAND_CHECK: {
        printf("Received CHECK packet from user %s and %hu bytes.\n", username,
               packet.length);
        Map *files_ok = hash_create();
        while (reader->read < packet.length) {
          char *path = read_string(reader);
          uint8_t file_hash[HASH_ALGORITHM_BYTE_LENGTH];
          read_u8(reader, file_hash, HASH_ALGORITHM_BYTE_LENGTH);
          unsigned long path_size = strlen(path);
          char in_user_dir_path[username_length + 1 + path_size + 1];
          sprintf(in_user_dir_path, "%s/%s", username, path);
          if (hash_has(path_descriptors, in_user_dir_path)) {
            hash_set(files_ok, path, NULL);
            free(path);
            continue;
          }
          if (access(in_user_dir_path, F_OK) != 0) {
            send_delete_message(client_connection, path);
          } else {
            uint8_t *current_file_hash = hash_file(in_user_dir_path);
            char out_path[sizeof("syncdir/") + path_size];
            sprintf(out_path, "syncdir/%s", path);
            if (memcmp(current_file_hash, file_hash,
                       HASH_ALGORITHM_BYTE_LENGTH) != 0)
              send_file(in_user_dir_path, out_path, username,
                        client_connection);
            free(current_file_hash);
          }
          hash_set(files_ok, path, NULL);
          free(path);
        }
        DIR *dir = opendir(username);
        struct dirent *directory_entry;
        while ((directory_entry = readdir(dir)) != NULL) {
          if (strcmp(directory_entry->d_name, ".") == 0 ||
              strcmp(directory_entry->d_name, "..") == 0)
            continue;
          if (hash_has(files_ok, directory_entry->d_name))
            continue;
          unsigned long dir_name_length = strlen(directory_entry->d_name);
          char in_path[username_length + 1 + dir_name_length];
          sprintf(in_path, "%s/%s", username, directory_entry->d_name);
          if (hash_has(path_descriptors, in_path))
            continue;
          char out_path[sizeof("syncdir/") + dir_name_length];
          sprintf(out_path, "syncdir/%s", directory_entry->d_name);
          send_file(in_path, out_path, username, client_connection);
        }
        closedir(dir);
        hash_destroy(files_ok);
        break;
      }
      }
      break;
    }
    case DATA: {
      decode_file(reader, username_length, username, packet);
      break;
    }
    }
    free(username);
    destroy_reader(reader);
  }
  printf("Closing connection %d...\n", client_connection);
  pthread_exit(0);
}

void decode_file(Reader *reader, unsigned long username_length, char username[],
                 Packet packet) {
  char *path = read_string(reader);
  unsigned long path_size = strlen(path);
  unsigned long out_path_size = path_size + username_length + 2;
  char out_path[out_path_size];
  sprintf(out_path, "%s/%s", username, path);
  printf("Decoding file %s: %d/%d\n", out_path, packet.sequence_number + 1,
         packet.total_size);
  if (!hash_has(path_descriptors, out_path)) {
    FILE *file = fopen(out_path, "wb");
    hash_set(path_descriptors, out_path, file);
  }

  FILE *file = hash_get(path_descriptors, out_path);
  fwrite(reader->buffer, sizeof(uint8_t), packet.length - reader->read, file);
  if (packet.sequence_number == packet.total_size - 1) {
    fclose(file);
    hash_remove(path_descriptors, out_path);
  }
  free(path);
}

void send_delete_message(int client_connection, char path[]) {
  Packet response_packet;
  response_packet.type = COMMAND;
  unsigned long client_delete_message_length =
      sizeof("./syncdir/") + strlen(path);
  char client_delete_message[client_delete_message_length];
  snprintf(client_delete_message, client_delete_message_length, "./syncdir/%s",
           path);
  response_packet.total_size = 1;
  response_packet.sequence_number = 0;
  Writer *writer = create_writer();
  if (writer == NULL) {
    printf(FAILED_TO_CREATE_WRITER_MESSAGE);
    return;
  }
  write_ulong(writer, COMMAND_DELETE);
  write_string(writer, client_delete_message);
  response_packet.length = writer->length,
  send(client_connection, &response_packet, sizeof(response_packet), 0);
  send(client_connection, writer->buffer, writer->length, 0);
  destroy_writer(writer);
}

void send_download_message(int client_connection, char path[]) {
  Packet packet;
  packet.type = COMMAND;
  packet.total_size = 1;
  packet.sequence_number = 0;
  Writer *writer = create_writer();
  if (writer == NULL) {
    printf(FAILED_TO_CREATE_WRITER_MESSAGE);
    return;
  }
  write_ulong(writer, COMMAND_DOWNLOAD);
  write_string(writer, path);
  packet.length = writer->length;
  printf("Sending DOWNLOAD request for file %s...\n", path);
  send(client_connection, &packet, sizeof(packet), 0);
  send(client_connection, writer->buffer, writer->length, 0);
  destroy_writer(writer);
}

void send_list_response(char username[], int client_connection) {
  unsigned long folder_path_length = sizeof("./") + strlen(username) + 1;
  char folder_path[folder_path_length];
  snprintf(folder_path, folder_path_length, "./%s", username);
  char response[4096];

  generate_file_list_string(folder_path, response, sizeof(response));

  unsigned long response_length = strlen(response);
  uint32_t fragment_count = ceil((double)response_length / PACKET_LENGTH);
  uint32_t current_fragment = 0;
  unsigned long bytes_sent = 0;
  Writer *writer = create_writer();
  if (writer == NULL) {
    printf(FAILED_TO_CREATE_WRITER_MESSAGE);
    return;
  }
  write_ulong(writer, COMMAND_LIST);
  write_string(writer, response);
  Packet response_packet;
  response_packet.type = COMMAND;
  response_packet.total_size = 1;
  response_packet.sequence_number = 0;
  response_packet.length = writer->length;
  send(client_connection, &response_packet, sizeof(response_packet), 0);
  send(client_connection, writer->buffer, writer->length, 0);
  destroy_writer(writer);
}

void close_socket() { close(server_client.file_descriptor); }
