#ifndef WRITER_H
#define WRITER_H
#include <stdint.h>
#include <stdio.h>

#define INITIAL_WRITER_SIZE 64

#define FAILED_TO_CREATE_WRITER_MESSAGE                                        \
  "Failed to create writer: out of memory\n"

typedef struct {
  uint8_t *buffer;
  size_t length;
  size_t capacity;
} Writer;

Writer *create_writer();
void destroy_writer(Writer *writer);

void write_bytes(Writer *writer, void *buf, size_t count);
void write_string(Writer *writer, char text[]);
void write_ulong(Writer *writer, unsigned long value);
#endif
