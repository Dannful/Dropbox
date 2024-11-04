#ifndef HASH_H
#define HASH_H
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>

typedef struct bucket {
  const char *key;
  void *value;
  struct bucket *next;
} Bucket;

typedef struct {
  Bucket **elements;
  size_t count;
  sem_t semaphore;
} Map;

Map *hash_create(void);
void hash_destroy(Map *map);
void *hash_get(Map *map, const char *key);
uint8_t hash_has(Map *map, const char *key);
void hash_set(Map *map, const char *key, void *value);
void hash_remove(Map *map, const char *key);
unsigned char *hash_file(char *file_name);
#endif
