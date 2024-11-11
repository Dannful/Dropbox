#include "../include/connection.h"
#include "../../core/hash.h"
#include "../../core/reader.h"
#include "../../core/utils.h"
#include "../../core/writer.h"
#include <arpa/inet.h>
#include <dirent.h>
#include <libgen.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

int control_connection = -1;
char username[USERNAME_LENGTH];
char hostname[253];
uint16_t server_port;

Map *path_descriptors = NULL;
Map *file_timestamps = NULL;
Map *files_writing = NULL;

pthread_mutex_t pooling_lock;
pthread_mutex_t watcher_lock;

void set_server_data(char host[], uint16_t port) {
  strcpy(hostname, host);
  server_port = port;
}

ConnectionResult open_connection(int *fd) {
  *fd = socket(AF_INET, SOCK_STREAM, 0);
  int status;
  struct sockaddr_in server_address;
  pthread_t handler, pooling;

  if (*fd < 0) {
    return CONNECTION_SOCKET_FAILRUE;
  }

  struct addrinfo hints = {0}, *res;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  if (getaddrinfo(hostname, NULL, &hints, &res) != 0)
    return CONNECTION_INVALID_ADDRESS;

  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(server_port);

  if (inet_pton(AF_INET,
                inet_ntoa(((struct sockaddr_in *)res->ai_addr)->sin_addr),
                &server_address.sin_addr) <= 0) {
    return CONNECTION_INVALID_ADDRESS;
  }

  if ((status = connect(*fd, (struct sockaddr *)&server_address,
                        sizeof(server_address))) < 0) {
    return CONNECT_FAILURE;
  }

  return SERVER_CONNECTION_SUCCESS;
}

ConnectionResult open_control_connection() {
  ConnectionResult result = open_connection(&control_connection);
  if (result == SERVER_CONNECTION_SUCCESS) {
    pthread_t handler, pooling;
    path_descriptors = hash_create();
    file_timestamps = hash_create();
    files_writing = hash_create();

    pthread_mutex_init(&pooling_lock, NULL);

    pthread_create(&handler, NULL, control_connection_handler, NULL);
    pthread_create(&pooling, NULL, pooling_manager, NULL);
  }
  return result;
}

void *pooling_manager(void *arg) {
  unsigned long username_length = strlen(username);
  unsigned long amount_to_sleep = 5;
  while (1) {
    if (access("./syncdir", F_OK) != 0) {
      sleep(amount_to_sleep);
      continue;
    }
    Packet check_packet;
    DIR *dir = opendir("./syncdir");
    check_packet.sequence_number = 0;
    check_packet.total_size = 1;
    check_packet.type = COMMAND;
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
      pthread_mutex_lock(&pooling_lock);
      if (hash_has(path_descriptors, directory_entry->d_name)) {
        pthread_mutex_unlock(&pooling_lock);
        continue;
      }
      struct stat attributes;
      unsigned long file_name_length = strlen(directory_entry->d_name);
      char full_path[sizeof("./syncdir/") - 1 + file_name_length];
      sprintf(full_path, "./syncdir/%s", directory_entry->d_name);
      if (hash_has(files_writing, full_path)) {
        pthread_mutex_unlock(&pooling_lock);
        continue;
      }
      uint8_t file_hash[HASH_ALGORITHM_BYTE_LENGTH];
      hash_file(file_hash, full_path);
      char file_hash_string[HASH_ALGORITHM_BYTE_LENGTH * 2 + 1] = {0};
      bytes_to_string(file_hash_string, file_hash, HASH_ALGORITHM_BYTE_LENGTH);
      printf("Including file %s in CHECK with hash %s\n",
             directory_entry->d_name, file_hash_string);
      write_string(writer, directory_entry->d_name);
      write_bytes(writer, file_hash, HASH_ALGORITHM_BYTE_LENGTH);
      pthread_mutex_unlock(&pooling_lock);
    }
    closedir(dir);
    check_packet.length = writer->length;
    printf("Sending CHECK packet with %lu bytes...\n", writer->length);
    if (send(control_connection, &check_packet, sizeof(Packet), 0) == 0 ||
        send(control_connection, writer->buffer, writer->length, 0) == 0) {
      destroy_writer(writer);
      exit(0);
      break;
    }
    destroy_writer(writer);
    sleep(amount_to_sleep);
  }
  pthread_exit(0);
}

uint8_t decode_file(Reader *reader, Packet packet) {
  read_string(reader);
  char *out_path = read_string(reader);
  unsigned long username_length = strlen(username);
  pthread_mutex_lock(&pooling_lock);
  pthread_mutex_lock(&watcher_lock);
  if (!hash_has(path_descriptors, basename(out_path))) {
    // we're setting this hash before AND after so that even if this thread
    // calls fopen to create the files and gets interrupted, it is in the
    // hash
    hash_set(path_descriptors, basename(out_path), NULL);
    FILE *file = fopen(out_path, "wb");
    hash_set(path_descriptors, basename(out_path), file);
  }
  pthread_mutex_unlock(&pooling_lock);
  pthread_mutex_unlock(&watcher_lock);

  printf("Decoding %hu bytes for file %s: %d/%d\n", packet.length, out_path,
         packet.sequence_number + 1, packet.total_size);
  FILE *file = hash_get(path_descriptors, basename(out_path));
  fwrite(reader->buffer, sizeof(uint8_t), packet.length - reader->read, file);
  struct stat attributes;
  stat(out_path, &attributes);
  hash_set(file_timestamps, basename(out_path),
           &(unsigned long){attributes.st_mtim.tv_sec});
  if (packet.sequence_number == packet.total_size - 1) {
    fclose(file);
    hash_remove(path_descriptors, basename(out_path));
    free(out_path);
    return 0;
  }
  free(out_path);
  return 1;
}

void *control_connection_handler(void *arg) {
  Packet decoded_packet;

  while (safe_recv(control_connection, &decoded_packet, sizeof(decoded_packet),
                   0) > 0) {
    uint8_t buffer[decoded_packet.length];
    if (safe_recv(control_connection, buffer, decoded_packet.length, 0) == 0)
      break;
    Reader *reader = create_reader(buffer);
    CommandType command = read_ulong(reader);

    switch (command) {
    case COMMAND_DOWNLOAD: {
      char *arguments = read_string(reader);
      unsigned long path_length = sizeof("./syncdir/") + strlen(arguments);
      char new_file_path[path_length];
      sprintf(new_file_path, "./syncdir/%s", arguments);
      free(arguments);
      printf("Received download request for file %s.\n", new_file_path);
      send_upload_message(new_file_path);
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
      printf("Received CHECK response from server with %d bytes.\n",
             decoded_packet.length);
      while (reader->read < decoded_packet.length) {
        char *path = read_string(reader);
        send_download_message(path, 1);
        free(path);
      }
      break;
    }
      destroy_reader(reader);
    }
  }
  return 0;
}

void *download_connection_handler(void *arg) {
  int connection = *((int *)arg);
  free(arg);
  while (1) {
    Packet packet;
    if (safe_recv(connection, &packet, sizeof(packet), 0) == 0)
      break;
    uint8_t buffer[packet.length];
    if (safe_recv(connection, buffer, packet.length, 0) == 0)
      break;
    Reader *reader = create_reader(buffer);
    if (decode_file(reader, packet) == 0) {
      destroy_reader(reader);
      break;
    }
    destroy_reader(reader);
  }
  close(connection);
  return 0;
}

void send_upload_message(char path[]) {
  char *base_path = basename(path);
  if (access(path, F_OK) != 0)
    return;
  int new_connection;
  ConnectionResult result = open_connection(&new_connection);
  if (result != SERVER_CONNECTION_SUCCESS) {
    printf("Failed to connect to server.\n");
    return;
  }
  pthread_t upload;
  FileData *data = malloc(sizeof(FileData));
  data->path_in = strdup(path);
  data->username = strdup(username);
  data->path_out = strdup(base_path);
  data->socket = new_connection;
  data->hash = files_writing;
  data->lock = &pooling_lock;
  pthread_create(&upload, NULL, send_file, data);
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
  send(control_connection, &packet, sizeof(packet), 0);
  send(control_connection, writer->buffer, writer->length, 0);
  destroy_writer(writer);
}

void send_download_message(char path[], uint8_t sync) {
  int *new_connection = malloc(sizeof(int));
  ConnectionResult result = open_connection(new_connection);
  if (sync && hash_has(path_descriptors, path))
    return;
  if (result != SERVER_CONNECTION_SUCCESS) {
    printf("Failed to open connection for download.\n");
    return;
  }
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
  write_bytes(writer, &sync, sizeof(sync));
  write_string(writer, path);
  packet.length = writer->length;
  send(*new_connection, &packet, sizeof(packet), 0);
  send(*new_connection, writer->buffer, writer->length, 0);
  pthread_t download_handler;
  printf("Sending download message for file %s...\n", path);
  pthread_create(&download_handler, NULL, download_connection_handler,
                 new_connection);
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
  send(control_connection, &packet, sizeof(packet), 0);
  send(control_connection, writer->buffer, writer->length, 0);
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
  send(control_connection, &packet, sizeof(Packet), 0);
  send(control_connection, writer->buffer, writer->length, 0);
  destroy_writer(writer);
}

void close_connection() {
  if (control_connection == -1) {
    printf("The connection has not been established!");
    return;
  }
  close(control_connection);
}

void set_username(char user[USERNAME_LENGTH]) { strcpy(username, user); }
