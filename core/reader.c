#include "./reader.h"
#include <stdlib.h>
#include <string.h>

Reader *create_reader(uint8_t *buffer) {
  Reader *reader = malloc(sizeof(Reader));
  reader->buffer = buffer;
  reader->read = 0;
  return reader;
}

void destroy_reader(Reader *reader) { free(reader); }

char *read_string(Reader *reader) {
  unsigned long string_length = strlen((char *)reader->buffer);
  char *dup = strdup((char *)reader->buffer);
  reader->buffer += string_length + 1;
  reader->read += string_length + 1;
  return dup;
}

unsigned long read_ulong(Reader *reader) {
  unsigned long result = *((unsigned long *)reader->buffer);
  reader->buffer += sizeof(unsigned long);
  reader->read += sizeof(unsigned long);
  return result;
}
