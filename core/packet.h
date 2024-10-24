#include <stdint.h>
#include <stdio.h>

#define PACKET_LENGTH 64
#define FILE_NAME_LENGTH 256
#define USERNAME_LENGTH 32

typedef enum { COMMAND = 0, DATA = 1 } MessageType;
typedef enum { DOWNLOAD = 0, DELETE = 1, LIST = 2, SYNC_DIR = 3 } CommandType;

typedef struct {
  MessageType type;
  uint32_t sequence_number;
  uint32_t total_size;
  uint16_t length;
} Packet;

typedef enum {
  PACKET_SEND_SOCKET_UNAVAILABLE = -1,
  PACKET_SEND_OK = 0,
  PACKET_SEND_SOCKET_CLOSED = 1,
} PacketSendResult;

void send_file(char path[], char username[], int socket);
