#include "hash.h"
#include "list.h"
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>

#define PACKET_LENGTH 1024
#define USERNAME_LENGTH 32

typedef enum { COMMAND = 0, DATA = 1, ELECTION = 2, HEARTBEAT = 3 } MessageType;
typedef enum {
  COMMAND_DOWNLOAD = 0,
  COMMAND_DELETE = 1,
  COMMAND_LIST = 2,
  COMMAND_SYNC_DIR = 3,
  COMMAND_CHECK = 4
} CommandType;

typedef struct {
  MessageType type;
  uint32_t sequence_number;
  uint32_t total_size;
  uint16_t length;
} Packet;

typedef struct {
  char *path_in;
  char *path_out;
  char *username;
  Map *hash;
  int socket;
  pthread_mutex_t *lock;
  List *list;
} FileData;

void *send_file(void *arg);
ssize_t safe_recv(int socket, void *buffer, size_t amount, int flags);
