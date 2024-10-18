#include "../include/connection.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

int socket_file_descriptor = -1;

ConnectionResult server_connect(char host[], u_int16_t port) {
  socket_file_descriptor = socket(AF_INET, SOCK_STREAM, 0);
  int status;
  struct sockaddr_in server_address;

  if (socket_file_descriptor < 0) {
    return SOCKET_FAILURE;
  }

  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(port);

  if (inet_pton(AF_INET, host, &server_address.sin_addr) <= 0) {
    return INVALID_ADDRESS;
  }

  if ((status =
           connect(socket_file_descriptor, (struct sockaddr *)&server_address,
                   sizeof(server_address))) < 0) {
    return CONNECT_FAILURE;
  }

  return SUCCESS;
}

void send_message(u_int8_t bytes[], unsigned int count) {
  if (socket_file_descriptor == -1) {
    printf("The connection has not been established!");
    return;
  }
  send(socket_file_descriptor, bytes, count, 0);
}

void close_connection() {
  if (socket_file_descriptor == -1) {
    printf("The connection has not been established!");
    return;
  }
  close(socket_file_descriptor);
}
