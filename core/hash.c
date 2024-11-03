#include "hash.h"
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
