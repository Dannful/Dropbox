#include "./packet.h"
#include <libgen.h>
#include <math.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>

void send_file(char path[], char username[], int socket) {
  printf("Sending file %s...\n", path);
  FILE *file = fopen(path, "rb");
  fseek(file, 0, SEEK_END);
  unsigned long file_size = ftell(file);
  rewind(file);
  uint32_t fragment_count = ceil((double)file_size / PACKET_LENGTH);
  printf("Fragment count: %d\n", fragment_count);
  unsigned long username_length = strlen(username) + 1;
  char *base_path = basename(path);
  unsigned long path_size = strlen(base_path) + 1;
  unsigned long bytes_read = 0;
  uint32_t current_fragment = 0;
  while (bytes_read < file_size) {
    unsigned int buffer_size = PACKET_LENGTH <= file_size - bytes_read
                                   ? PACKET_LENGTH
                                   : file_size - bytes_read;
    uint8_t buffer[buffer_size];
    Packet packet;
    unsigned long read_from_file =
        fread(buffer, sizeof(uint8_t), buffer_size, file);
    packet.length = read_from_file + username_length + path_size;
    bytes_read += read_from_file;
    packet.type = DATA;
    packet.sequence_number = current_fragment++;
    packet.total_size = fragment_count;
    uint8_t data[packet.length];
    memmove(data, username, username_length);
    memmove(data + username_length, base_path, path_size);
    memmove(data + username_length + path_size, buffer, read_from_file);
    send(socket, &packet, sizeof(packet), 0);
    send(socket, data, packet.length, 0);
  }
  fclose(file);
}
