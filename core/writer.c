#include "./writer.h"
#include <stdlib.h>
#include <string.h>

Writer *create_writer() {
  Writer *writer = malloc(sizeof(Writer));
  writer->buffer = calloc(sizeof(uint8_t), INITIAL_WRITER_SIZE);
  writer->capacity = INITIAL_WRITER_SIZE;
  writer->length = 0;
  return writer;
}
void destroy_writer(Writer *writer) {
  free(writer->buffer);
  free(writer);
}

void write_bytes(Writer *writer, void *buf, size_t count) {
  if (writer->length + count > writer->capacity) {
    writer->buffer =
        realloc(writer->buffer, writer->capacity + count + INITIAL_WRITER_SIZE);
    writer->capacity += count + INITIAL_WRITER_SIZE;
  }
  memcpy(writer->buffer + writer->length, buf, count);
  writer->length += count;
}

void write_string(Writer *writer, char text[]) {
  write_bytes(writer, text, strlen(text) + 1);
}
void write_ulong(Writer *writer, unsigned long value) {
  write_bytes(writer, &value, sizeof(unsigned long));
}
