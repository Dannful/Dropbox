#include "./packet.h"
#include <libgen.h>
#include <math.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>

void send_file(char path_in[], char path_out[], char username[], int socket) {
  printf("Sending file %s...\n", path_in);
  FILE *file = fopen(path_in, "rb");
  fseek(file, 0, SEEK_END);
  unsigned long file_size = ftell(file);
  rewind(file);
  uint32_t fragment_count = ceil((double)file_size / PACKET_LENGTH);
  unsigned long username_length = strlen(username) + 1;
  unsigned long path_size = strlen(path_out) + 1;
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
    memmove(data + username_length, path_out, path_size);
    memmove(data + username_length + path_size, buffer, read_from_file);
    send(socket, &packet, sizeof(packet), 0);
    send(socket, data, packet.length, 0);
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

unsigned long hash(char str[], unsigned int size) {
  unsigned long hash = 5381;
  int c;

  while ((c = *str++))
    hash = ((hash << 5) + hash) + c;

  return hash % size;
}

void decode_file(FILE *path_descriptors[], char out_path[], uint8_t buffer[],
                 unsigned long username_length, char username[],
                 Packet packet) {
  unsigned long hashed_index = hash(out_path, 1024);
  unsigned long out_path_size = strlen(out_path) + 1;
  unsigned long original_path_size =
      strlen((char *)(buffer + username_length)) + 1;
  char out_path_part[out_path_size + 5];
  snprintf(out_path_part, out_path_size + 5, "%s.part", out_path);
  if (path_descriptors[hashed_index] == NULL) {
    path_descriptors[hashed_index] = fopen(out_path_part, "wb");
  }

  FILE *file = path_descriptors[hashed_index];
  fwrite(buffer + username_length + original_path_size, sizeof(uint8_t),
         packet.length - username_length - original_path_size, file);
  if (packet.sequence_number == packet.total_size - 1) {
    remove(out_path);
    rename(out_path_part, out_path);
    fclose(file);
    path_descriptors[hashed_index] = NULL;
  }
}
