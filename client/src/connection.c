#include "../include/connection.h"
#include <arpa/inet.h>
#include <libgen.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int socket_file_descriptor = -1;
char username[USERNAME_LENGTH];

ConnectionResult server_connect(char host[], u_int16_t port) {
  socket_file_descriptor = socket(AF_INET, SOCK_STREAM, 0);
  int status;
  struct sockaddr_in server_address;

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

  return SERVER_CONNECTION_SUCCESS;
}

void send_upload_message(char path[]) {
  if (socket_file_descriptor == -1) {
    printf("The connection has not been established!");
    exit(1);
    return;
  }
  send_file(path, username, socket_file_descriptor);
}

void send_download_message(char path[]) {
  Packet packet;
  packet.type = COMMAND;
  unsigned long message_length = strlen("DLD ") + strlen(path) + 1;
  unsigned long username_length = strlen(username) + 1;
  char download_message[message_length];
  sprintf(download_message, "DLD %s", path);
  packet.length = message_length + username_length;
  packet.total_size = message_length;
  packet.sequence_number = message_length;
  send(socket_file_descriptor, &packet, sizeof(packet), 0);
  send(socket_file_descriptor, username, username_length, 0);
  send(socket_file_descriptor, download_message, message_length, 0);
}

void send_delete_message(char path[]) {
  Packet packet;
  packet.type = COMMAND;
  unsigned long message_length = strlen("DEL ") + strlen(path) + 1;
  unsigned long username_length = strlen(username) + 1;
  char delete_message[message_length];
  sprintf(delete_message, "DEL %s", path);
  packet.length = message_length + username_length;
  packet.total_size = message_length;
  packet.sequence_number = message_length;
  send(socket_file_descriptor, &packet, sizeof(packet), 0);
  send(socket_file_descriptor, username, username_length, 0);
  send(socket_file_descriptor, delete_message, message_length, 0);
}

char *send_list_server_message();
char *send_list_client_message();
void send_sync_dir_message() {
  Packet packet;
  packet.type = COMMAND;
  char message[] = "SYN";
  unsigned long length = strlen(message) + 1;
  unsigned long username_length = strlen(username) + 1;
  packet.length = length + username_length;
  packet.sequence_number = length;
  packet.total_size = length;
  uint8_t data[packet.length];
  memmove(data, username, username_length);
  memmove(data + username_length, message, length);
  send(socket_file_descriptor, &packet, sizeof(packet), 0);
  send(socket_file_descriptor, data, packet.length, 0);
}

void close_connection() {
  if (socket_file_descriptor == -1) {
    printf("The connection has not been established!");
    return;
  }
  close(socket_file_descriptor);
}

void set_username(char user[USERNAME_LENGTH]) { strcpy(username, user); }
