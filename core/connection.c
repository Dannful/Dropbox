#include "connection.h"
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>

ConnectionResult open_connection(int *fd, char *host, uint16_t port) {
  *fd = socket(AF_INET, SOCK_STREAM, 0);
  int status;
  struct sockaddr_in server_address;
  pthread_t handler, pooling;

  if (*fd < 0) {
    return CONNECTION_SOCKET_FAILURE;
  }

  struct addrinfo hints = {0}, *res;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  if (getaddrinfo(host, NULL, &hints, &res) != 0)
    return CONNECTION_INVALID_ADDRESS;

  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(port);

  if (inet_pton(AF_INET,
                inet_ntoa(((struct sockaddr_in *)res->ai_addr)->sin_addr),
                &server_address.sin_addr) <= 0) {
    return CONNECTION_INVALID_ADDRESS;
  }

  if ((status = connect(*fd, (struct sockaddr *)&server_address,
                        sizeof(server_address))) < 0) {
    return CONNECT_FAILURE;
  }

  printf("Created new connection %d.\n", *fd);

  return SERVER_CONNECTION_SUCCESS;
}
