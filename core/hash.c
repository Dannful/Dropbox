#include "hash.h"
#include <openssl/core.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <stdlib.h>
#include <string.h>

#define SIZE 1024

size_t hash(const char *key, size_t size) {
  unsigned long hash = 5381;
  int c;

  while ((c = *key++))
    hash = ((hash << 5) + hash) + c;

  return hash % size;
}

Map *hash_create(void) {
  Map *map = malloc(sizeof(Map));
  map->count = 0;
  map->elements = calloc(sizeof(Bucket *), SIZE);
  sem_init(&map->semaphore, 0, 1);
  return map;
}

void hash_destroy(Map *map) {
  for (int i = 0; i < SIZE; i++) {
    Bucket *bucket = map->elements[i];
    while (bucket) {
      Bucket *aux = bucket->next;
      free(bucket);
      bucket = aux;
    }
  }
  sem_destroy(&map->semaphore);
  free(map->elements);
  free(map);
}

void *hash_get(Map *map, const char *key) {
  sem_wait(&map->semaphore);
  Bucket *bucket = map->elements[hash(key, SIZE)];
  while (bucket != NULL) {
    if (strcmp(bucket->key, key) == 0) {
      sem_post(&map->semaphore);
      return bucket->value;
    }
    bucket = bucket->next;
  }
  sem_post(&map->semaphore);
  return NULL;
}

uint8_t hash_has(Map *map, const char *key) {
  sem_wait(&map->semaphore);
  Bucket *bucket = map->elements[hash(key, SIZE)];
  while (bucket != NULL) {
    if (strcmp(bucket->key, key) == 0) {
      sem_post(&map->semaphore);
      return 1;
    }
    bucket = bucket->next;
  }
  sem_post(&map->semaphore);
  return 0;
}

void hash_set(Map *map, const char *key, void *value) {
  size_t index = hash(key, SIZE);
  Bucket *bucket = map->elements[index];
  Bucket *target = NULL;
  sem_wait(&map->semaphore);
  if (bucket == NULL) {
    map->elements[index] = malloc(sizeof(Bucket));
    target = map->elements[index];
  }
  while (target == NULL) {
    if (strcmp(bucket->key, key) == 0)
      return;
    if (bucket->next == NULL) {
      target = malloc(sizeof(Bucket));
      bucket->next = target;
    } else {
      bucket = bucket->next;
    }
  }
  target->key = strdup(key);
  target->value = value;
  target->next = NULL;
  map->count++;
  sem_post(&map->semaphore);
}

void hash_remove(Map *map, const char *key) {
  size_t index = hash(key, SIZE);
  sem_wait(&map->semaphore);
  Bucket *bucket = map->elements[index];
  if (bucket == NULL)
    return;
  if (strcmp(bucket->key, key) == 0) {
    map->elements[index] = bucket->next;
    free(bucket);
    sem_post(&map->semaphore);
    return;
  }
  while (strcmp(bucket->next->key, key) != 0)
    bucket = bucket->next;
  Bucket *aux = bucket->next;
  bucket->next = aux->next;
  free((void *)aux->key);
  free(aux);
  map->count--;
  sem_post(&map->semaphore);
}

unsigned char *hash_file(char *file_name) {
  unsigned char *result = malloc(2 * SHA_DIGEST_LENGTH);
  unsigned char hash[SHA_DIGEST_LENGTH];
  int i;
  FILE *f = fopen(file_name, "rb");
  EVP_MD_CTX *mdContent = EVP_MD_CTX_new();
  int bytes;
  unsigned char data[1024];

  EVP_DigestInit_ex(mdContent, EVP_sha256(), NULL);

  while ((bytes = fread(data, 1, 1024, f)) != 0) {
    EVP_DigestUpdate(mdContent, data, bytes);
  }

  unsigned int length = SHA256_DIGEST_LENGTH;
  EVP_DigestFinal(mdContent, hash, &length);
  EVP_MD_CTX_free(mdContent);

  for (i = 0; i < SHA_DIGEST_LENGTH; i++) {
    sprintf((char *)&(result[i * 2]), "%02x", hash[i]);
  }

  fclose(f);

  return result;
}
