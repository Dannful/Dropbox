#pragma once

#include "../../core/packet.h"
#include "../../core/reader.h"
#include <stdint.h>
#include <netinet/in.h>

#define BASE_PORT 8080

typedef struct {
  pthread_mutex_t file_lock;
  pthread_mutex_t sync_dir_lock;
} UserLocks;

typedef struct {
  int connection_1;
  int connection_2;
} UserConnections;

typedef enum {
  SERVER_ACCEPT_FAILURE = -4,
  SERVER_SOCKET_CREATION_FAILURE = -3,
  SERVER_SOCKET_LISTEN_FAILURE = -2,
  SERVER_SOCKET_BIND_FAILURE = -1,
  SERVER_SUCCESS = 0
} ServerBindResult;

typedef struct {
  uint8_t id;
  char *hostname;
  uint16_t port;
} ServerReplica;

ServerBindResult server_listen(int *fd, struct sockaddr_in *address, uint16_t port);
ServerBindResult create_control_socket(uint16_t port);
ServerBindResult create_cluster_socket(uint16_t port);
char *get_user_file(char username[], char file[]);
char *get_user_syncdir_file(char file[]);
void send_message(uint8_t bytes[], unsigned int count);
void *handle_client_connection(void *arg);
void *handle_cluster_connection(void *arg);
void *thread_control_listen(void *arg);
void *thread_cluster_listen(void *arg);
void close_socket();
void send_download_message(int client_connection, char path[]);
void send_delete_message(int client_connection, char username[], char path[]);
void decode_file(Reader *reader, unsigned long username_length, char username[],
                 Packet packet, int socket);
void send_list_response(char username[], int client_connection);
void send_upload_message(int client_connection, char username[], char path_in[],
                         char path_out[]);
void deallocate();

uint8_t get_number_of_replicas();
void set_number_of_replicas(uint8_t replicas);

uint8_t get_replica_id();
void set_replica_id(uint8_t replica_id);

void register_server_replica(ServerReplica replica);
ServerReplica *get_server_replica(uint8_t id);
void *thread_ping();

uint16_t get_control_port();
uint16_t get_cluster_port();
void set_control_port(uint16_t port);
void set_cluster_port(uint16_t port);
