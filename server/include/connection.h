#ifndef CONNECTION_H
#define CONNECTION_H

#include "../../core/packet.h"
#include "../../core/reader.h"
#include <stdint.h>

typedef enum {
  SERVER_ACCEPT_FAILURE = -4,
  SERVER_SOCKET_CREATION_FAILURE = -3,
  SERVER_SOCKET_LISTEN_FAILURE = -2,
  SERVER_SOCKET_BIND_FAILURE = -1,
  SERVER_CONNECTION_SUCCESS = 0
} ConnectionResult;

typedef struct {
  pthread_mutex_t send_file_lock;
  pthread_mutex_t check_lock;
} UserLocks;

ConnectionResult server_listen(uint16_t port);
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
#endif
