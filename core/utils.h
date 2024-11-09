#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>
#include <stdio.h>

void generate_file_list_string(char dir_path[], char buffer[],
                               size_t buffer_size);
void bytes_to_string(char *out, uint8_t *bytes, size_t count);
#endif
