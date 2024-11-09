#ifndef HASH_H
#define HASH_H
#include <openssl/core.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>

#define HASH_ALGORITHM_BYTE_LENGTH SHA256_DIGEST_LENGTH

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
void hash_free_content(Map *map);
void hash_destroy(Map *map);
void *hash_get(Map *map, const char *key);
uint8_t hash_has(Map *map, const char *key);
void hash_set(Map *map, const char *key, void *value);
void hash_remove(Map *map, const char *key);
void hash_file(void *out, char *file_name);
#endif
