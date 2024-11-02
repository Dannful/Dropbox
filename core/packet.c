#include "./packet.h"
#include "writer.h"
#include <libgen.h>
#include <math.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <utime.h>

void send_file(char path_in[], char path_out[], char username[], int socket) {
  printf("Sending file %s to %s...\n", path_in, path_out);
  FILE *file = fopen(path_in, "rb");
  fseek(file, 0, SEEK_END);
  unsigned long file_size = ftell(file);
  rewind(file);
  uint32_t fragment_count = ceil((double)file_size / PACKET_LENGTH);
  unsigned long username_length = strlen(username) + 1;
  unsigned long path_size = strlen(path_out) + 1;
  unsigned long bytes_read = 0;
  uint32_t current_fragment = 0;
  struct stat attributes;
  stat(path_in, &attributes);
  while (bytes_read < file_size) {
    unsigned int buffer_size = PACKET_LENGTH <= file_size - bytes_read
                                   ? PACKET_LENGTH
                                   : file_size - bytes_read;
    uint8_t buffer[buffer_size];
    Packet packet;
    unsigned long read_from_file =
        fread(buffer, sizeof(uint8_t), buffer_size, file);
    bytes_read += read_from_file;
    packet.type = DATA;
    packet.sequence_number = current_fragment++;
    packet.total_size = fragment_count;
    Writer *writer = create_writer();
    unsigned long timestamp = attributes.st_ctim.tv_sec;
    write_string(writer, username);
    write_string(writer, path_out);
    write_ulong(writer, timestamp);
    write_bytes(writer, buffer, read_from_file);
    packet.length = writer->length;
    send(socket, &packet, sizeof(packet), 0);
    send(socket, writer->buffer, writer->length, 0);
    destroy_writer(writer);
  }
  fclose(file);
}

ssize_t safe_recv(int socket, void *buffer, size_t amount, int flags) {
  ssize_t read = 0;
  while (read < amount) {
    if ((read += recv(socket, buffer + read, amount - read, flags)) == 0)
      return 0;
  }
  return amount;
}
