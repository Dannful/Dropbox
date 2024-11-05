#include "../include/connection.h"
#include "../../core/hash.h"
#include "../../core/reader.h"
#include "../../core/writer.h"
#include <arpa/inet.h>
#include <dirent.h>
#include <libgen.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

int socket_file_descriptor = -1;
char username[USERNAME_LENGTH];
Map *path_descriptors = NULL;
Map *file_timestamps = NULL;

sem_t pooling_semaphore;

ConnectionResult server_connect(char host[], u_int16_t port) {
  socket_file_descriptor = socket(AF_INET, SOCK_STREAM, 0);
  int status;
  struct sockaddr_in server_address;
  pthread_t handler, pooling;

  if (socket_file_descriptor < 0) {
    return CONNECTION_SOCKET_FAILRUE;
  }

  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(port);

  if (inet_pton(AF_INET, host, &server_address.sin_addr) <= 0) {
    return CONNECTION_INVALID_ADDRESS;
  }

  if ((status =
           connect(socket_file_descriptor, (struct sockaddr *)&server_address,
                   sizeof(server_address))) < 0) {
    return CONNECT_FAILURE;
  }

  path_descriptors = hash_create();
  file_timestamps = hash_create();
  sem_init(&pooling_semaphore, 0, 1);
  pthread_create(&handler, NULL, connection_handler, NULL);
  pthread_create(&pooling, NULL, pooling_manager, NULL);

  return SERVER_CONNECTION_SUCCESS;
}

void *pooling_manager(void *arg) {
  unsigned long username_length = strlen(username);
  unsigned long amount_to_sleep = 5;
  while (1) {
    if (access("./syncdir", F_OK) != 0) {
      sleep(amount_to_sleep);
      continue;
    }
    Packet sync_packet;
    DIR *dir = opendir("./syncdir");
    sync_packet.sequence_number = 0;
    sync_packet.total_size = 1;
    sync_packet.type = COMMAND;
    struct dirent *directory_entry;
    Writer *writer = create_writer();
    if (writer == NULL) {
      printf(FAILED_TO_CREATE_WRITER_MESSAGE);
      continue;
    }
    write_string(writer, username);
    write_ulong(writer, COMMAND_CHECK);
    while ((directory_entry = readdir(dir)) != NULL) {
      if (strcmp(directory_entry->d_name, ".") == 0 ||
          strcmp(directory_entry->d_name, "..") == 0)
        continue;
      sem_wait(&pooling_semaphore);
      if (hash_has(path_descriptors, directory_entry->d_name)) {
        continue;
      }
      struct stat attributes;
      unsigned long file_name_length = strlen(directory_entry->d_name);
      char full_path[sizeof("./syncdir/") - 1 + file_name_length];
      sprintf(full_path, "./syncdir/%s", directory_entry->d_name);
      uint8_t *file_hash = hash_file(full_path);
      write_string(writer, directory_entry->d_name);
      write_bytes(writer, file_hash, HASH_ALGORITHM_BYTE_LENGTH);
      free(file_hash);
      sem_post(&pooling_semaphore);
    }
    closedir(dir);
    sync_packet.length = writer->length;
    printf("Sending CHECK packet with %lu bytes...\n", writer->length);
    if (send(socket_file_descriptor, &sync_packet, sizeof(Packet), 0) == 0 ||
        send(socket_file_descriptor, writer->buffer, writer->length, 0) == 0) {
      destroy_writer(writer);
      break;
    }
    destroy_writer(writer);
    sleep(amount_to_sleep);
  }
  pthread_exit(0);
}

void decode_file(Reader *reader, Packet packet) {
  read_string(reader);
  char *out_path = read_string(reader);
  unsigned long username_length = strlen(username);
  sem_wait(&pooling_semaphore);
  if (!hash_has(path_descriptors, basename(out_path))) {
    FILE *file = fopen(out_path, "wb");
    hash_set(path_descriptors, basename(out_path), file);
  }

  FILE *file = hash_get(path_descriptors, basename(out_path));
  fwrite(reader->buffer, sizeof(uint8_t), packet.length - reader->read, file);
  struct stat attributes;
  stat(out_path, &attributes);
  hash_set(file_timestamps, basename(out_path),
           &(unsigned long){attributes.st_mtim.tv_sec});
  if (packet.sequence_number == packet.total_size - 1) {
    fclose(file);
    hash_remove(path_descriptors, basename(out_path));
  }
  sem_post(&pooling_semaphore);
  free(out_path);
}

void *connection_handler(void *arg) {
  Packet decoded_packet;

  while (safe_recv(socket_file_descriptor, &decoded_packet,
                   sizeof(decoded_packet), 0) > 0) {
    uint8_t buffer[decoded_packet.length];
    safe_recv(socket_file_descriptor, buffer, decoded_packet.length, 0);
    Reader *reader = create_reader(buffer);
    switch (decoded_packet.type) {
    case COMMAND: {
      CommandType command = read_ulong(reader);

      switch (command) {
      case COMMAND_DOWNLOAD: {
        char *arguments = read_string(reader);
        unsigned long path_length = sizeof("./syncdir/") + strlen(arguments);
        char new_file_path[path_length];
        sprintf(new_file_path, "./syncdir/%s", arguments);
        free(arguments);
        printf("Received download request for file %s.\n", new_file_path);
        send_file(new_file_path, arguments, username, socket_file_descriptor);
        break;
      }
      case COMMAND_DELETE: {
        char *arguments = read_string(reader);
        remove(arguments);
        free(arguments);
        break;
      }
      case COMMAND_LIST: {
        char *response = read_string(reader);
        printf("%s", response);
        free(response);
        break;
      }
      case COMMAND_SYNC_DIR: {
        break;
      }
      case COMMAND_CHECK: {
        break;
      }
      }
      break;
    }
    case DATA: {
      decode_file(reader, decoded_packet);
      break;
    }
    }
    destroy_reader(reader);
  }
  pthread_exit(0);
}

void send_upload_message(char path[]) {
  if (socket_file_descriptor == -1) {
    printf("The connection has not been established!");
    exit(1);
    return;
  }
  char *base_path = basename(path);
  send_file(path, base_path, username, socket_file_descriptor);
}

void send_list_server_message() {
  Packet packet;
  packet.type = COMMAND;
  packet.total_size = 1;
  packet.sequence_number = 0;
  Writer *writer = create_writer();
  if (writer == NULL) {
    printf(FAILED_TO_CREATE_WRITER_MESSAGE);
    return;
  }
  write_string(writer, username);
  write_ulong(writer, COMMAND_LIST);
  packet.length = writer->length;
  send(socket_file_descriptor, &packet, sizeof(packet), 0);
  send(socket_file_descriptor, writer->buffer, writer->length, 0);
  destroy_writer(writer);
}

void send_download_message(char path[]) {
  Packet packet;
  packet.type = COMMAND;
  packet.total_size = 1;
  packet.sequence_number = 0;
  Writer *writer = create_writer();
  if (writer == NULL) {
    printf(FAILED_TO_CREATE_WRITER_MESSAGE);
    return;
  }
  write_string(writer, username);
  write_ulong(writer, COMMAND_DOWNLOAD);
  packet.length = writer->length;
  send(socket_file_descriptor, &packet, sizeof(packet), 0);
  send(socket_file_descriptor, writer->buffer, writer->length, 0);
  destroy_writer(writer);
}

void send_delete_message(char path[]) {
  Packet packet;
  packet.type = COMMAND;
  packet.total_size = 1;
  packet.sequence_number = 0;
  Writer *writer = create_writer();
  if (writer == NULL) {
    printf(FAILED_TO_CREATE_WRITER_MESSAGE);
    return;
  }
  write_string(writer, username);
  write_ulong(writer, COMMAND_DELETE);
  write_string(writer, path);
  packet.length = writer->length;
  send(socket_file_descriptor, &packet, sizeof(packet), 0);
  send(socket_file_descriptor, writer->buffer, writer->length, 0);
  destroy_writer(writer);
}

void send_sync_dir_message() {
  Packet packet;
  packet.type = COMMAND;
  packet.sequence_number = 0;
  packet.total_size = 1;
  Writer *writer = create_writer();
  if (writer == NULL) {
    printf(FAILED_TO_CREATE_WRITER_MESSAGE);
    return;
  }
  write_string(writer, username);
  write_ulong(writer, COMMAND_SYNC_DIR);
  packet.length = writer->length;
  send(socket_file_descriptor, &packet, sizeof(Packet), 0);
  send(socket_file_descriptor, writer->buffer, writer->length, 0);
  destroy_writer(writer);
}

void close_connection() {
  if (socket_file_descriptor == -1) {
    printf("The connection has not been established!");
    return;
  }
  close(socket_file_descriptor);
}

void set_username(char user[USERNAME_LENGTH]) { strcpy(username, user); }
