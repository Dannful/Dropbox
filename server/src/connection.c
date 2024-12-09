#include "../include/connection.h"
#include "../../core/connection.h"
#include "../../core/list.h"
#include "../../core/utils.h"
#include "../../core/writer.h"
#include "../include/election.h"
#include "math.h"
#include <dirent.h>
#include <libgen.h>
#include <netdb.h>
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
#include <arpa/inet.h>

#define CONTROL_PORT 8000

#define PENDING_CONNECTIONS_BUFFER 5
#define PING_THREAD_INTERVAL_SECONDS 6

typedef struct {
  struct sockaddr_in address;
  int file_descriptor;
  struct sockaddr_in cluster_address;
  int cluster_fd;
  socklen_t length;
  socklen_t cluster_length;
} client;

client server_client;
Map *path_descriptors = NULL;
Map *files_writing = NULL;
Map *user_locks = NULL;
Map *connected_users = NULL;
Map *connection_files = NULL;
Map *pending_servers = NULL;
Map *pending_ack_delete = NULL;

ServerReplica *server_replicas = NULL;

uint8_t number_of_replicas = 1;
uint8_t replica_id = 0;

uint16_t control_port, cluster_port;

ServerBindResult server_listen(int *fd, struct sockaddr_in *address, u_int16_t port) {
  *fd = socket(AF_INET, SOCK_STREAM, 0);
  int optval = 1;
  setsockopt(*fd, SOL_SOCKET, SO_REUSEADDR, &optval,
             sizeof(optval));

  if (*fd == -1) {
    return SERVER_SOCKET_CREATION_FAILURE;
  }
  bzero(address, sizeof(*address));

  address->sin_family = AF_INET;
  address->sin_addr.s_addr = INADDR_ANY;
  address->sin_port = htons(port);

  if ((bind(*fd,
            (struct sockaddr *)address,
            sizeof(*address))) < 0) {
    return SERVER_SOCKET_BIND_FAILURE;
  }

  if ((listen(*fd, PENDING_CONNECTIONS_BUFFER)) < 0) {
    return SERVER_SOCKET_LISTEN_FAILURE;
  }

  printf("Server bound with socket %d. Spawning LISTEN thread...\n",
    *fd);

  return SERVER_SUCCESS;
}

ServerBindResult create_control_socket(uint16_t port) {
  ServerBindResult result = server_listen(&server_client.file_descriptor, &server_client.address, port);
  if(result == SERVER_SUCCESS) {
    pthread_t listen_thread;
    pending_servers = hash_create();
    user_locks = hash_create();
    files_writing = hash_create();
    connected_users = hash_create();
    connection_files = hash_create();
    pending_ack_delete = hash_create();
    pthread_create(&listen_thread, NULL, thread_control_listen, NULL);
  }
  return result;
}

ServerBindResult create_cluster_socket(uint16_t port) {
  ServerBindResult result = server_listen(&server_client.cluster_fd, &server_client.cluster_address, port);
  if(result == SERVER_SUCCESS) {
    pthread_t ping_thread, cluster_thread;
    pthread_create(&ping_thread, NULL, thread_ping, NULL);
    pthread_create(&cluster_thread, NULL, thread_cluster_listen, NULL);
  }
  return result;
}

void *thread_control_listen(void *arg) {
  while (1) {
    uint8_t *primary_server = get_primary_server();
    if(primary_server == NULL || *primary_server != get_replica_id()) {
      sleep(PING_THREAD_INTERVAL_SECONDS);
      continue;
    }
    printf("CONTROL: Listening for connections...\n");
    pthread_t main, backup;
    UserConnection *connection = malloc(sizeof(UserConnection));
    struct sockaddr_in addr;
    if ((connection->fd = accept(server_client.file_descriptor,
                                 (struct sockaddr *)&addr,
                                 &server_client.length)) < 0) {
      pthread_exit((void *)1);
    }
    
    strcpy(connection->address, (char*)inet_ntoa((struct in_addr)addr.sin_addr));
    printf("CONTROL: Received client connection request: %d. Spawning thread...\n",
      connection->fd);
    pthread_create(&main, NULL, handle_client_connection, connection);
  }
  pthread_exit(0);
}

void *thread_cluster_listen(void *arg) {
  while (1) {
    printf("CLUSTER: Listening for connections...\n");
    pthread_t main;
    int *client_socket = malloc(sizeof(int));
    if ((*client_socket = accept(server_client.cluster_fd,
                                 (struct sockaddr *)&server_client.cluster_address,
                                 &server_client.length)) < 0) {
      pthread_exit((void *)1);
    }
    printf("CLUSTER: Received client connection request: %d. Spawning thread...\n",
           *client_socket);
    pthread_create(&main, NULL, handle_cluster_connection, client_socket);
  }
  pthread_exit(0);
}

void *thread_ping() {
  while (1) {
    if(get_primary_server() == NULL || *get_primary_server() == get_replica_id()) {
      sleep(PING_THREAD_INTERVAL_SECONDS);
      continue;
    }
    if(send_heartbeat_message() == BEGIN_ELECTION)
      send_election_message(0, get_replica_id());
    sleep(PING_THREAD_INTERVAL_SECONDS);
  }
}

void *handle_cluster_connection(void *arg) {
  int client_connection = *((int *)arg);
  free(arg);

  while(1) {
    Packet packet;
    printf("CLUSTER: Waiting to read bytes from socket %d...\n", client_connection);
    if (safe_recv(client_connection, &packet, sizeof(packet), 0) != sizeof(packet))
      break;
    uint8_t buffer[packet.length];
    if (safe_recv(client_connection, buffer, packet.length, 0) != packet.length)
      break;
    printf("CLUSTER: Received message of type %d from connection %d and %d bytes.\n",
           packet.type, client_connection, packet.length);
    Reader *reader = create_reader(buffer);
    if(packet.type == COMMAND) {
      CommandType command = read_ulong(reader);

      if(command == COMMAND_SYNC_DIR) {
        struct stat st = {0};
        char *username = read_string(reader);
        uint8_t is_delete;
        read_u8(reader, &is_delete, sizeof(is_delete));
        printf("Received SYNC_DIR for user %s.\n", username);

        if(!is_delete) {
          UserLocks *locks = (UserLocks *)hash_get(user_locks, username);
          if (locks == NULL) {
            locks = malloc(sizeof(UserLocks));
            pthread_mutex_init(&locks->file_lock, NULL);
            pthread_mutex_init(&locks->sync_dir_lock, NULL);
            hash_set(user_locks, username, locks);
          }
          if (stat(username, &st) == -1)
            mkdir(username, 0700);
          UserConnections *connections = calloc(1, sizeof(UserConnections));
          hash_set(connected_users, username, connections);
          read_u8(reader, connections, sizeof(UserConnections));
        } else {
          hash_remove(connected_users, username);
        }
        free(username);
      } else if(command == COMMAND_DELETE) {
        uint8_t ack;
        read_u8(reader, &ack, sizeof(ack));
        char *path = read_string(reader);
        printf("CLUSTER: Received DELETE packet with ack %d and path %s.\n", ack, path);
        if(ack) {
          uint8_t *count = (uint8_t*) hash_get(pending_ack_delete, path);
          (*count)++;
          destroy_reader(reader);
          break;
        } else {
          remove(path);
          Packet ack_packet;
          ack_packet.sequence_number = 0;
          ack_packet.length = 1;
          ack_packet.type = COMMAND;
          Writer *writer = create_writer();
          write_ulong(writer, COMMAND_DELETE);
          ack = 1;
          write_bytes(writer, &ack, sizeof(ack));
          write_string(writer, path);
          ack_packet.length = writer->length;

          ServerReplica *primary = get_server_replica(*get_primary_server());
          int fd;
          if(primary != NULL && open_connection(&fd, primary->hostname, primary->port) == SERVER_CONNECTION_SUCCESS) {
            send(fd, &ack_packet, sizeof(ack_packet), 0);
            send(fd, writer->buffer, writer->length, 0);
          }
        }
      }
    } else if(packet.type == ELECTION) {
      uint8_t is_election_over, elected, dead[get_number_of_replicas()];
      read_u8(reader, &is_election_over, sizeof(uint8_t));
      read_u8(reader, &elected, sizeof(uint8_t));
      read_u8(reader, dead, sizeof(uint8_t) * get_number_of_replicas());
      printf("Received ELECTION message. Election over: %d. Elected: %d.\n", is_election_over, elected);
      receive_election_message(is_election_over, elected, dead);
    } else if(packet.type == HEARTBEAT) {
      uint8_t response_needed, sender_id;
      read_u8(reader, &response_needed, sizeof(response_needed));
      read_u8(reader, &sender_id, sizeof(sender_id));
      printf("Received HEARTBEAT from server %d.\n", sender_id);
      revive_server(sender_id);
      if(!response_needed) {
        destroy_reader(reader);
        return 0;
      }
      Packet response;
      response.type = HEARTBEAT;
      response.sequence_number = 0;
      response.total_size = 1;
      Writer *writer = create_writer();
      uint8_t id = get_replica_id();
      response_needed = 0;
      write_bytes(writer, &response_needed, sizeof(response_needed));
      write_bytes(writer, &id, sizeof(id));
      response.length = writer->length;
      send(client_connection, &response, sizeof(Packet), 0);
      send(client_connection, writer->buffer, writer->length, 0);
    } else if(packet.type == DATA) {
      char *username = read_string(reader);
      unsigned long username_length = strlen(username);
      decode_file(reader, username_length, username, packet, client_connection);
      free(username);
    } else if(packet.type == DATA_OK) {
      char *path = read_string(reader);
      uint8_t *current_count = (uint8_t*) hash_get(pending_servers, path);
      printf("Received DATA OK packet for %s. Current count is %d.\n", path, *current_count);
      (*current_count)++;
      if(*current_count == get_number_of_replicas())
        hash_remove(pending_servers, path);
      free(path);
      break;
    }
    destroy_reader(reader);
  }
  printf("CLUSTER: Closing connection %d...\n", client_connection);
  close(client_connection);
  return 0;
}

void *handle_client_connection(void *arg) {
  UserConnection connection = *((UserConnection*)arg);
  free(arg);
  char *username = NULL;

  int error = 0;
  socklen_t length = sizeof(error);
  unsigned int read_bytes = 0;
  char client_connection_key[(int)ceil(log10(connection.fd) + 1) + 1];
  sprintf(client_connection_key, "%d", connection.fd);
  hash_set(connection_files, client_connection_key, create_list());
  hash_set(files_writing, client_connection_key, hash_create());

  while (1) {
    if(get_primary_server() == NULL || *get_primary_server() != get_replica_id()) {
      printf("Dammit, I'm not the chosen one! Taking a nap...\n");
      sleep(6);
      continue;
    }
    Packet packet;
    struct stat st = {0};

    printf("Waiting to read bytes from socket %d...\n", connection.fd);

    if (safe_recv(connection.fd, &packet, sizeof(packet), 0) == 0)
      break;
    printf("Received message of type %d from connection %d and %d bytes.\n",
           packet.type, connection.fd, packet.length);
    uint8_t buffer[packet.length];
    if (safe_recv(connection.fd, buffer, packet.length, 0) == 0)
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
        if (!hash_has(path_descriptors, new_file_path) && !hash_has(pending_servers, new_file_path)) {
          List *list = hash_get(connection_files, client_connection_key);
          if (!list_contains(list, new_file_path, strlen(new_file_path) + 1)) {
            send_upload_message(connection.fd, username, new_file_path,
                                sync ? in_sync_dir : arguments);
          } else {
            goto end;
          }
        } else {
          goto end;
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

        uint8_t *deletions = malloc(sizeof(uint8_t));
        *deletions = 1;
        hash_set(pending_ack_delete, new_file_path, deletions);

        for(int i = 0; i < get_number_of_replicas(); i++) {
          if(i != *get_primary_server()) {
            int fd;
            ServerReplica *replica = get_server_replica(i);
            if(replica == NULL)
              continue;
            if(open_connection(&fd, replica->hostname, replica->port) != SERVER_CONNECTION_SUCCESS)
              continue;
            Packet sync;
            sync.type = COMMAND;
            sync.sequence_number = 0;
            sync.total_size = 1;
            Writer *writer = create_writer();
            uint8_t ack = 0;
            write_ulong(writer, COMMAND_DELETE);
            write_bytes(writer, &ack, sizeof(ack));
            write_string(writer, new_file_path);
            sync.length = writer->length;
            send(fd, &sync, sizeof(sync), 0);
            send(fd, writer->buffer, writer->length, 0);
          }
        }

        send_delete_message(connection.fd, username, arguments);
        free(arguments);
        free(new_file_path);
        break;
      }
      case COMMAND_LIST: {
        printf("Received LST request from user %s.\n", username);
        send_list_response(username, connection.fd);
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
          user->first.fd = connection.fd;
          user->second.fd = -1;
          strcpy(user->first.address, connection.address);
        } else if (user->first.fd == connection.fd ||
                   user->second.fd == connection.fd) {
          continue;
        } else if (user->first.fd == -1) {
          user->first.fd = connection.fd;
          strcpy(user->first.address, connection.address);
        } else if (user->second.fd == -1) {
          user->second.fd = connection.fd;
          strcpy(user->second.address, connection.address);
        } else {
          printf(
              "User %s already connected to two clients. Closing connection.\n",
              username);
          destroy_reader(reader);
          free(username);
          close(connection.fd);
          pthread_mutex_unlock(&locks->sync_dir_lock);
          pthread_exit(0);
        }
        for(int i = 0; i < get_number_of_replicas(); i++) {
          if(i != *get_primary_server()) {
            int fd;
            ServerReplica *replica = get_server_replica(i);
            if(replica == NULL)
              continue;
            if(open_connection(&fd, replica->hostname, replica->port) != SERVER_CONNECTION_SUCCESS)
              continue;
            Packet sync;
            sync.type = COMMAND;
            sync.sequence_number = 0;
            sync.total_size = 1;
            Writer *writer = create_writer();
            write_ulong(writer, COMMAND_SYNC_DIR);
            write_string(writer, username);
            uint8_t is_delete = 0;
            write_bytes(writer, &is_delete, sizeof(uint8_t));
            write_bytes(writer, user, sizeof(UserConnections));
            sync.length = writer->length;
            send(fd, &sync, sizeof(sync), 0);
            send(fd, writer->buffer, writer->length, 0);
            destroy_writer(writer);
          }
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
            send_delete_message(connection.fd, username, path);
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
        if (send(connection.fd, &response, sizeof(response), 0) <= 0) {
          destroy_writer(out);
          break;
        }
        if (send(connection.fd, out->buffer, out->length, 0) <= 0) {
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
      decode_file(reader, username_length, username, packet, -1);
      break;
    }
    case HEARTBEAT: {
      break;
    }
    case ELECTION: {
      break;
    }
    case DATA_OK: {
      break;
    }
    }
    destroy_reader(reader);
  }
  end:
  printf("Closing connection %d...\n", connection.fd);
  void *user_ptr = hash_get(connected_users, username);
  if (user_ptr != NULL) {
    UserConnections *user = (UserConnections *)user_ptr;
    if (user->first.fd == connection.fd) {
      user->first.fd = -1;
    } else if (user->second.fd == connection.fd) {
      user->second.fd = -1;
    }

    if (user->first.fd == -1 && user->second.fd == -1) {
      printf("Removing user %s from connected users.\n", username);
      hash_remove(connected_users, username);
      free(user);
      for(int i = 0; i < get_number_of_replicas(); i++) {
        if(i != *get_primary_server()) {
          int fd;
          ServerReplica *replica = get_server_replica(i);
          if(replica == NULL)
            continue;
          if(open_connection(&fd, replica->hostname, replica->port) != SERVER_CONNECTION_SUCCESS)
            continue;
          Packet sync;
          sync.type = COMMAND;
          sync.sequence_number = 0;
          sync.total_size = 1;
          Writer *writer = create_writer();
          write_ulong(writer, COMMAND_SYNC_DIR);
          write_string(writer, username);
          uint8_t is_delete = 1;
          write_bytes(writer, &is_delete, sizeof(uint8_t));
          sync.length = writer->length;
          send(fd, &sync, sizeof(sync), 0);
          send(fd, writer->buffer, writer->length, 0);
          destroy_writer(writer);
        }
      }
    }
  }
  void *client_connection_files =
      hash_get(connection_files, client_connection_key);
  if (client_connection_files != NULL)
    list_destroy(hash_get(connection_files, client_connection_key));
  hash_remove(connection_files, client_connection_key);

  if (username != NULL)
    free(username);
  close(connection.fd);
  pthread_exit(0);
}

void reconnect_to_clients() {
  if (connected_users == NULL) {
    return;
  }
  int fd = -1;
  for(int i = 0; i < connected_users->size; i++) {
    if(connected_users->elements[i] != NULL) {
      Bucket *bucket = connected_users->elements[i];
      
      const char *username = bucket->key;
      UserConnections connections = *((UserConnections*) bucket->value);

      if(connections.first.fd != -1) {
      
        printf("Attempting to connect to %s's first device on %s:6666.", username, connections.first.address);
        while(open_connection(&fd, connections.first.address, 6666) != SERVER_CONNECTION_SUCCESS){
          sleep(3);
        }
        printf("%s's first device succesfully connected. Sending port and closing...\n", username);
        if(send(fd, &control_port, sizeof(control_port), 0)<= 0) {
          printf("Error sending port.\n");
        }
        close(fd);
      }

      if(connections.second.fd != -1) {
      
        printf("Attempting to connect to %s's second device on %s:6666.", username, connections.second.address);
        while(open_connection(&fd, connections.second.address, 6666) != SERVER_CONNECTION_SUCCESS){
          sleep(3);
        }
        printf("%s's second device succesfully connected. Sending port and closing...\n", username);
        if(send(fd, &control_port, sizeof(control_port), 0)<= 0) {
          printf("Error sending port.\n");
        }
        close(fd);
      }
    }
  }
}


void send_file_to_servers(char *path, char *username) {
  for(int replica_id = 0; replica_id < get_number_of_replicas(); replica_id++) {
    if(replica_id == *get_primary_server())
      continue;
    ServerReplica *replica = get_server_replica(replica_id);
    if(replica == NULL)
      continue;
    int fd = -1;
    if(open_connection(&fd, replica->hostname, replica->port) != SERVER_CONNECTION_SUCCESS)
      continue;
    FileData *data = malloc(sizeof(FileData));
    data->path_in = strdup(get_user_file(username, path));
    data->path_out = strdup(path);
    data->hash = NULL;
    data->list = NULL;
    data->lock = NULL;
    data->username = strdup(username);
    data->socket = fd;

    pthread_t file_send_thread, handle_connection;

    pthread_create(&file_send_thread, NULL, send_file, data);
  }
}

void decode_file(Reader *reader, unsigned long username_length, char username[],
                 Packet packet, int socket) {
  char *path = read_string(reader);
  char *out_path = get_user_file(username, path);
  UserLocks *locks = user_locks != NULL ? (UserLocks *)hash_get(user_locks, username) : NULL;
  while (socket == -1 && locks == NULL)
    ;
  if (packet.sequence_number == 0) {
    printf("File %s is trying to acquire lock.\n", out_path);
    if(locks != NULL)
      pthread_mutex_lock(&locks->file_lock);
  }
  while (files_writing != NULL && hash_has(files_writing, out_path))
    printf("File %s is being sent.\n", out_path);
  unsigned long path_size = strlen(path);
  printf("Decoding %hu bytes for file %s: %d/%d\n", packet.length, out_path,
         packet.sequence_number + 1, packet.total_size);
  if (packet.length == username_length + 1 + path_size + 1) {
    FILE *file = fopen(out_path, "wb");
    fclose(file);
    if (locks != NULL && packet.sequence_number == 0)
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
    if(socket != -1) {
      close(socket);
      ServerReplica *replica = get_server_replica(*get_primary_server());
      int ok_conn;
      if(replica != NULL && open_connection(&ok_conn, replica->hostname, replica->port) == SERVER_CONNECTION_SUCCESS) {
        printf("Sending DATA OK packet for file %s...\n", out_path);
        Packet ok;
        ok.type = DATA_OK;
        ok.sequence_number = 0;
        ok.total_size = 1;
        Writer *writer = create_writer();
        write_string(writer, out_path);
        ok.length = writer->length;
        send(ok_conn, &ok, sizeof(ok), 0);
        send(ok_conn, writer->buffer, writer->length, 0);
        destroy_writer(writer);
      }
    } else if(get_replica_id() == *get_primary_server()) {
      uint8_t *count = malloc(sizeof(uint8_t));
      *count = 1;
      hash_set(pending_servers, out_path, count);
      send_file_to_servers(path, username);
    }
    if(locks != NULL)
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

void send_delete_message(int client_connection, char username[], char path[]) {
  char *in_server_file = get_user_file(username, basename(path));
  void *value = hash_get(pending_ack_delete, in_server_file);
  if(value != NULL && *((uint8_t*) value) < get_number_of_replicas()) {
    free(in_server_file);
    return;
  }
  hash_remove(pending_ack_delete, in_server_file);
  char *client_delete_message = get_user_syncdir_file(basename(path));
  Packet response_packet;
  response_packet.type = COMMAND;
  response_packet.total_size = 1;
  response_packet.sequence_number = 0;
  Writer *writer = create_writer();
  if (writer == NULL) {
    printf(FAILED_TO_CREATE_WRITER_MESSAGE);
    free(in_server_file);
    free(client_delete_message);
    return;
  }
  write_ulong(writer, COMMAND_DELETE);
  write_string(writer, client_delete_message);
  response_packet.length = writer->length,
  send(client_connection, &response_packet, sizeof(response_packet), 0);
  send(client_connection, writer->buffer, writer->length, 0);
  destroy_writer(writer);
  free(in_server_file);
  free(client_delete_message);
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

uint16_t get_control_port() {
  return control_port;
}

uint16_t get_cluster_port() {
  return cluster_port;
}

void set_control_port(uint16_t port) {
  control_port = port;
}

void set_cluster_port(uint16_t port) {
  cluster_port = port;
}
