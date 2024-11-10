#include "./packet.h"
#include "writer.h"
#include <libgen.h>
#include <math.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <utime.h>

void *send_file(void *arg) {
  FileData file_data;
  memmove(&file_data, arg, sizeof(FileData));
  free(arg);
  if (file_data.lock != NULL)
    pthread_mutex_lock(file_data.lock);
  hash_set(file_data.hash, file_data.path_in, NULL);
  if (file_data.lock != NULL)
    pthread_mutex_unlock(file_data.lock);
  FILE *file = fopen(file_data.path_in, "rb");
  fseek(file, 0, SEEK_END);
  unsigned long file_size = ftell(file);
  rewind(file);
  printf("Beginning file read...\n");
  if (file_size == 0) {
    Packet packet;
    packet.total_size = 1;
    packet.sequence_number = 0;
    packet.length = 0;
    packet.type = DATA;
    Writer *writer = create_writer();
    write_string(writer, file_data.username);
    write_string(writer, file_data.path_out);
    packet.length = writer->length;
    send(file_data.socket, &packet, sizeof(packet), 0);
    send(file_data.socket, writer->buffer, writer->length, 0);
    fclose(file);
    hash_remove(file_data.hash, file_data.path_in);
    free(file_data.path_in);
    free(file_data.path_out);
    free(file_data.username);
    destroy_writer(writer);
    return 0;
  }
  uint32_t fragment_count = ceil((double)file_size / PACKET_LENGTH);
  unsigned long username_length = strlen(file_data.username) + 1;
  unsigned long path_size = strlen(file_data.path_out) + 1;
  unsigned long bytes_read = 0;
  uint32_t current_fragment = 0;
  int counter = 0;
  while (bytes_read < file_size) {
    unsigned int buffer_size = PACKET_LENGTH;
    uint8_t buffer[buffer_size];
    Packet packet;
    unsigned long read_from_file =
        fread(buffer, sizeof(uint8_t), buffer_size, file);
    if (read_from_file == 0) {
      printf("Could not read anything from %s.\n", file_data.path_in);
      break;
    }
    bytes_read += read_from_file;
    packet.type = DATA;
    packet.sequence_number = current_fragment++;
    packet.total_size = fragment_count;
    Writer *writer = create_writer();
    if (writer == NULL) {
      printf(FAILED_TO_CREATE_WRITER_MESSAGE);
      break;
    }
    write_string(writer, file_data.username);
    write_string(writer, file_data.path_out);
    write_bytes(writer, buffer, read_from_file);
    packet.length = writer->length;
    printf("Sending %lu bytes of file %s to %s at %s: %d/%d\n", read_from_file,
           file_data.path_in, file_data.username, file_data.path_out,
           packet.sequence_number + 1, packet.total_size);
    if (send(file_data.socket, &packet, sizeof(packet), 0) == 0) {
      printf("Failed to send file %s: socket closed.\n", file_data.path_in);
      destroy_writer(writer);
      break;
    }
    if (send(file_data.socket, writer->buffer, writer->length, 0) == 0) {
      printf("Failed to send file %s: socket closed.\n", file_data.path_in);
      destroy_writer(writer);
      break;
    }
    destroy_writer(writer);
  }
  printf("Closing file %s...\n", file_data.path_in);
  fclose(file);
  hash_remove(file_data.hash, file_data.path_in);
  free(file_data.path_out);
  free(file_data.username);
  free(file_data.path_in);
  return 0;
}

ssize_t safe_recv(int socket, void *buffer, size_t amount, int flags) {
  ssize_t read = 0;
  while (read < amount) {
    if ((read += recv(socket, buffer + read, amount - read, flags)) == 0)
      return 0;
  }
  return amount;
}
