#include "../include/connection.h"
#include <arpa/inet.h>
#include <math.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int socket_file_descriptor = -1;

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
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    printf("Failed to open file %s\n.", path);
    return;
  }
  fseek(file, 0, SEEK_END);
  unsigned int file_size = ftell(file);
  rewind(file);
  unsigned int current_fragment = 0;
  unsigned int fragment_count = ceil((double)file_size / PACKET_LENGTH);
  unsigned int bytes_left = 0;
  while ((bytes_left = file_size - ftell(file)) > 0) {
    unsigned int buffer_size =
        PACKET_LENGTH <= bytes_left ? PACKET_LENGTH : bytes_left;
    uint8_t buffer[buffer_size];
    unsigned int read_bytes = fread(buffer, sizeof(uint8_t), buffer_size, file);
    Packet packet;
    packet.message_type = DATA;
    packet.data.data_packet.data_length = read_bytes;
    packet.data.data_packet.fragment_count = fragment_count;
    packet.data.data_packet.sequence_number = current_fragment++;
    packet.data.data_packet.data = buffer;
    char test[] = "/home/dannly/Documents/CS/Personal/C/Dropbox/dest.txt";
    strcpy(packet.data.data_packet.path, test);
    send_packet(socket_file_descriptor, packet);
  }
}

void send_download_message(char path[]);

void send_delete_message(char path[]);

char *send_list_server_message();
char *send_list_client_message();
void send_sync_dir_message() {}

void close_connection() {
  if (socket_file_descriptor == -1) {
    printf("The connection has not been established!");
    return;
  }
  close(socket_file_descriptor);
}
