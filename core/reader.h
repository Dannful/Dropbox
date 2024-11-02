#ifndef READER_H
#define READER_H
#include <stdint.h>
#include <stdio.h>

typedef struct {
  uint8_t *buffer;
  size_t read;
} Reader;

Reader *create_reader(uint8_t *buffer);
void destroy_reader(Reader *reader);

char *read_string(Reader *reader);
unsigned long read_ulong(Reader *reader);
#endif
