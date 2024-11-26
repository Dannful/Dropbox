#ifndef CONNECTION_H
#define CONNECTION_H

#include "../../core/packet.h"
#include "../../core/reader.h"
#include <stdint.h>

#define BASE_PORT 8080

typedef struct {
  pthread_mutex_t file_lock;
} UserLocks;

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
  uint8_t port;
} ServerReplica;

ServerBindResult server_listen(uint16_t port);
char *get_user_file(char username[], char file[]);
char *get_user_syncdir_file(char file[]);
void send_message(uint8_t bytes[], unsigned int count);
void *handle_client_connection(void *arg);
void *thread_listen(void *arg);
void close_socket();
void send_download_message(int client_connection, char path[]);
void send_delete_message(int client_connection, char path[]);
void decode_file(Reader *reader, unsigned long username_length, char username[],
                 Packet packet);
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

#endif
