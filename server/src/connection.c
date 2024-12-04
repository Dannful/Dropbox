#include "../include/connection.h"
#include "../../core/connection.h"
#include "../../core/list.h"
#include "../../core/utils.h"
#include "../../core/writer.h"
#include "../include/election.h"
#include "math.h"
#include "signal.h"
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
#define PING_THREAD_INTERVAL_SECONDS 6

typedef struct {
  struct sockaddr_in address;
  int file_descriptor;
  socklen_t length;
} client;

client server_client;
Map *path_descriptors;
Map *files_writing;
Map *user_locks;
Map *connected_users;
Map *connection_files;
Map *pending_servers;

ServerReplica *server_replicas = NULL;

uint8_t number_of_replicas = 1;
uint8_t replica_id = 0;

ServerBindResult server_listen(u_int16_t port) {
  pthread_t listen_thread, ping_thread;

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
  user_locks = hash_create();
  files_writing = hash_create();
  connected_users = hash_create();
  connection_files = hash_create();
  pending_servers = hash_create();

  signal(SIGPIPE, SIG_IGN);

  pthread_create(&listen_thread, NULL, thread_listen, NULL);
  pthread_create(&ping_thread, NULL, thread_ping, NULL);

  return SERVER_SUCCESS;
}

void *thread_listen(void *arg) {
  printf("Listening for connections...\n");
  while (1) {
    pthread_t thread;
    int *client_socket = malloc(sizeof(int));
    if ((*client_socket = accept(server_client.file_descriptor,
                                 (struct sockaddr *)&server_client.address,
                                 &server_client.length)) < 0) {
      pthread_exit((void *)1);
    }
    printf("Received client connection request: %d. Spawning thread...\n",
           *client_socket);
    pthread_create(&thread, NULL, handle_client_connection, client_socket);
  }
  pthread_exit(0);
}

void *thread_ping() {
  send_election_message(0, replica_id);
  while (1) {
    send_heartbeat_message();
    sleep(PING_THREAD_INTERVAL_SECONDS);
  }
}

void *handle_client_connection(void *arg) {
  int client_connection = *((int *)arg);
  free(arg);
  char *username = NULL;

  int error = 0;
  socklen_t length = sizeof(error);
  unsigned int read_bytes = 0;
  char client_connection_key[(int)ceil(log10(client_connection) + 1) + 1];
  sprintf(client_connection_key, "%d", client_connection);
  hash_set(connection_files, client_connection_key, create_list());
  hash_set(files_writing, client_connection_key, hash_create());
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
    username = read_string(reader);
    unsigned long username_length = strlen(username);

    switch (packet.type) {
    case COMMAND: {
      CommandType command = read_ulong(reader);

      switch (command) {
      case COMMAND_DOWNLOAD: {
        uint8_t sync;
        read_u8(reader, &sync, sizeof(sync));
        char *arguments = read_string(reader);
        unsigned long command_path_length = strlen(arguments);
        char *new_file_path = get_user_file(username, arguments);
        printf("Received download request from user %s and file %s.\n",
               username, new_file_path);
        char *in_sync_dir = get_user_syncdir_file(arguments);
        if (!hash_has(path_descriptors, new_file_path)) {
          List *list = hash_get(connection_files, client_connection_key);
          if (!list_contains(list, new_file_path, strlen(new_file_path) + 1)) {
            send_upload_message(client_connection, username, new_file_path,
                                sync ? in_sync_dir : arguments);
          } else {
            close(client_connection);
          }
        } else {
          close(client_connection);
        }
        free(arguments);
        free(new_file_path);
        free(in_sync_dir);
        break;
      }
      case COMMAND_DELETE: {
        char *arguments = read_string(reader);
        unsigned long command_path_length = strlen(arguments);
        char *new_file_path = get_user_file(username, arguments);
        printf("Received DELETE request from user %s and path %s.\n", username,
               arguments);
        if (remove(new_file_path) != 0)
          break;
        send_delete_message(client_connection, arguments);
        free(arguments);
        free(new_file_path);
        break;
      }
      case COMMAND_LIST: {
        printf("Received LST request from user %s.\n", username);
        send_list_response(username, client_connection);
        break;
      }
      case COMMAND_SYNC_DIR: {
        printf("Received SYN request from user %s.\n", username);

        UserLocks *locks = (UserLocks *)hash_get(user_locks, username);
        if (locks == NULL) {
          printf("Creating user lock for %s...\n", username);
          locks = malloc(sizeof(UserLocks));
          pthread_mutex_init(&locks->file_lock, NULL);
          pthread_mutex_init(&locks->sync_dir_lock, NULL);
          hash_set(user_locks, username, locks);
        }
        if (stat(username, &st) == -1) {
          mkdir(username, 0700);
        }

        pthread_mutex_lock(&locks->sync_dir_lock);
        UserConnections *user =
            (UserConnections *)hash_get(connected_users, username);

        if (user == NULL) {
          user = calloc(1, sizeof(UserConnections));
          user->connection_1 = client_connection;
          user->connection_2 = -1;
        } else if (user->connection_1 == client_connection ||
                   user->connection_2 == client_connection) {
          continue;
        } else if (user->connection_1 == -1) {
          user->connection_1 = client_connection;
        } else if (user->connection_2 == -1) {
          user->connection_2 = client_connection;
        } else {
          printf(
              "User %s already connected to two clients. Closing connection.\n",
              username);
          destroy_reader(reader);
          free(username);
          close(client_connection);
          pthread_mutex_unlock(&locks->sync_dir_lock);
          pthread_exit(0);
        }

        hash_set(connected_users, username, user);
        pthread_mutex_unlock(&locks->sync_dir_lock);

        break;
      }
      case COMMAND_CHECK: {
        printf("Received CHECK packet from user %s and %hu bytes.\n", username,
               packet.length);
        Map *files_ok = hash_create();
        Writer *out = create_writer();
        write_ulong(out, COMMAND_CHECK);
        while (reader->read < packet.length) {
          char *path = read_string(reader);
          uint8_t file_hash[HASH_ALGORITHM_BYTE_LENGTH];
          read_u8(reader, file_hash, HASH_ALGORITHM_BYTE_LENGTH);
          char file_hash_string[HASH_ALGORITHM_BYTE_LENGTH * 2 + 1];
          bytes_to_string(file_hash_string, file_hash,
                          HASH_ALGORITHM_BYTE_LENGTH);
          printf("Checking file %s with hash %s\n", path, file_hash_string);
          unsigned long path_size = strlen(path);
          char *in_user_dir_path = get_user_file(username, path);
          if (hash_has(path_descriptors, in_user_dir_path)) {
            hash_set(files_ok, path, NULL);
            free(path);
            free(in_user_dir_path);
            continue;
          }
          if (access(in_user_dir_path, F_OK) != 0) {
            send_delete_message(client_connection, path);
          } else {
            uint8_t current_file_hash[HASH_ALGORITHM_BYTE_LENGTH] = {0};
            hash_file(current_file_hash, in_user_dir_path);
            char *out_path = get_user_syncdir_file(path);
            if (memcmp(current_file_hash, file_hash,
                       HASH_ALGORITHM_BYTE_LENGTH) != 0) {
              printf("Including file %s in CHECK response.\n", path);
              write_string(out, path);
            }
            free(out_path);
          }
          hash_set(files_ok, path, NULL);
          free(path);
          free(in_user_dir_path);
        }
        DIR *dir = opendir(username);
        struct dirent *directory_entry;
        while ((directory_entry = readdir(dir)) != NULL) {
          if (strcmp(directory_entry->d_name, ".") == 0 ||
              strcmp(directory_entry->d_name, "..") == 0)
            continue;
          if (hash_has(files_ok, directory_entry->d_name))
            continue;
          char *in_folder = get_user_file(username, directory_entry->d_name);
          if (hash_has(path_descriptors, in_folder))
            continue;
          write_string(out, directory_entry->d_name);
          printf("Including file %s in CHECK response.\n",
                 directory_entry->d_name);
          free(in_folder);
        }
        Packet response;
        response.type = COMMAND;
        response.length = out->length;
        response.sequence_number = 0;
        response.total_size = 1;
        closedir(dir);
        hash_destroy(files_ok);
        printf("Sending %d bytes of CHECK response to user %s...\n",
               response.length, username);
        if (send(client_connection, &response, sizeof(response), 0) <= 0) {
          destroy_writer(out);
          break;
        }
        if (send(client_connection, out->buffer, out->length, 0) <= 0) {
          destroy_writer(out);
          break;
        }
        destroy_writer(out);
        break;
      }
      }
      break;
    }
    case DATA: {
      decode_file(reader, username_length, username, packet);
      break;
    }
    case HEARTBEAT: {
      if (send_heartbeat_message() == BEGIN_ELECTION)
        send_election_message(0, get_replica_id());
      break;
    }
    case ELECTION: {
      uint8_t is_election_over, elected, dead[get_number_of_replicas()];
      read_u8(reader, &is_election_over, sizeof(uint8_t));
      read_u8(reader, &elected, sizeof(uint8_t));
      read_u8(reader, dead, sizeof(uint8_t) * get_number_of_replicas());
      receive_election_message(is_election_over, elected, dead);
      break;
    }
    }
    destroy_reader(reader);
  }
  printf("Closing connection %d...\n", client_connection);
  void *user_ptr = hash_get(connected_users, username);
  if (user_ptr != NULL) {
    UserConnections *user = (UserConnections *)user_ptr;
    if (user->connection_1 == client_connection) {
      user->connection_1 = -1;
    } else if (user->connection_2 == client_connection) {
      user->connection_2 = -1;
    }

    if (user->connection_1 == -1 && user->connection_2 == -1) {
      printf("Removing user %s from connected users.\n", username);
      hash_remove(connected_users, username);
      free(user);
    }
  }
  void *client_connection_files =
      hash_get(connection_files, client_connection_key);
  if (client_connection_files != NULL)
    list_destroy(hash_get(connection_files, client_connection_key));
  hash_remove(connection_files, client_connection_key);

  if (username != NULL)
    free(username);
  close(client_connection);
  pthread_exit(0);
}

void send_file_to_servers(char path[], char username[]) {
  for(int replica_id = 0; replica_id < get_number_of_replicas(); replica_id++) {
    ServerReplica *replica = get_server_replica(replica_id);
    if(replica == NULL)
      continue;
    int fd = -1;
    if(open_connection(&fd, replica->hostname, replica->port) != SERVER_CONNECTION_SUCCESS)
      continue;
    FileData *data = malloc(sizeof(FileData));
    data->path_in = strdup(path);
    data->path_out = get_user_file(username, path);
    data->hash = NULL;
    data->list = NULL;
    data->socket = fd;

    pthread_t file_send_thread;

    pthread_create(&file_send_thread, NULL, send_file, data);
  }
}

void decode_file(Reader *reader, unsigned long username_length, char username[],
                 Packet packet) {
  char *path = read_string(reader);
  char *out_path = get_user_file(username, path);
  UserLocks *locks = (UserLocks *)hash_get(user_locks, username);
  while (locks == NULL)
    ;
  if (packet.sequence_number == 0) {
    printf("File %s is trying to acquire lock.\n", out_path);
    pthread_mutex_lock(&locks->file_lock);
  }
  while (hash_has(files_writing, out_path))
    printf("File %s is being sent.\n", out_path);
  unsigned long path_size = strlen(path);
  printf("Decoding %hu bytes for file %s: %d/%d\n", packet.length, out_path,
         packet.sequence_number + 1, packet.total_size);
  if (packet.length == username_length + 1 + path_size + 1) {
    FILE *file = fopen(out_path, "wb");
    fclose(file);
    if (packet.sequence_number == 0)
      pthread_mutex_unlock(&locks->file_lock);
    free(path);
    return;
  }
  if (!hash_has(path_descriptors, out_path)) {
    FILE *file = fopen(out_path, "wb");
    hash_set(path_descriptors, out_path, file);
  }

  FILE *file = hash_get(path_descriptors, out_path);
  fwrite(reader->buffer, sizeof(uint8_t), packet.length - reader->read, file);
  if (packet.sequence_number == packet.total_size - 1) {
    fclose(file);
    hash_remove(path_descriptors, out_path);
    pthread_mutex_unlock(&locks->file_lock);
  }
  free(out_path);
  free(path);
}

void send_upload_message(int client_connection, char username[], char path_in[],
                         char path_out[]) {
  pthread_t upload;
  FileData *data = malloc(sizeof(FileData));
  data->socket = client_connection;
  data->path_in = strdup(path_in);
  data->path_out = strdup(path_out);
  data->username = strdup(username);
  data->lock = &((UserLocks *)hash_get(user_locks, username))->file_lock;
  char key[(int)ceil(log10(client_connection) + 1) + 1];
  sprintf(key, "%d", client_connection);
  data->list = hash_get(connection_files, key);
  data->hash = hash_get(files_writing, key);
  pthread_create(&upload, NULL, send_file, data);
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

void deallocate() {
  extern uint8_t *primary_server, *in_election, *dead;
  close(server_client.file_descriptor);
  hash_destroy(path_descriptors);
  hash_free_content(user_locks);
  hash_destroy(user_locks);
  hash_destroy(files_writing);
  hash_free_content(connected_users);
  hash_destroy(connected_users);
  for (int i = 0; i < connection_files->size; i++)
    if (connection_files->elements[i] != NULL)
      list_destroy(connection_files->elements[i]->value);
  hash_destroy(connection_files);
  hash_destroy(pending_servers);
  free(server_replicas);
  free(primary_server);
  free(dead);
  if (in_election != NULL)
    free(in_election);
}

char *get_user_file(char username[], char file[]) {
  file = basename(file);
  char *user_folder =
      calloc(sizeof(char), 2 + strlen(username) + 1 + strlen(file) + 1);
  sprintf(user_folder, "./%s/%s", username, file);
  return user_folder;
}

char *get_user_syncdir_file(char file[]) {
  file = basename(file);
  char *sync_dir = calloc(sizeof(char), sizeof("./syncdir/") + strlen(file));
  sprintf(sync_dir, "./syncdir/%s", file);
  return sync_dir;
}

uint8_t get_number_of_replicas() { return number_of_replicas; }
void set_number_of_replicas(uint8_t replicas) {
  extern uint8_t *dead;
  number_of_replicas = replicas;
  if (server_replicas == NULL) {
    server_replicas = malloc(replicas * sizeof(ServerReplica));
    dead = calloc(replicas, sizeof(uint8_t));
    return;
  }
  server_replicas = realloc(server_replicas, replicas * sizeof(ServerReplica));
  dead = realloc(dead, replicas * sizeof(uint8_t));
}

uint8_t get_replica_id() { return replica_id; }
void set_replica_id(uint8_t id) { replica_id = id; }

void register_server_replica(ServerReplica replica) {
  server_replicas[replica.id] = replica;
}
ServerReplica *get_server_replica(uint8_t id) { return server_replicas + id; }
