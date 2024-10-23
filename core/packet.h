#include <stdint.h>

#define PACKET_LENGTH 64
#define FILE_NAME_LENGTH 256

typedef enum { COMMAND = 0, DATA = 1 } MessageType;
typedef enum { DOWNLOAD = 0, DELETE = 1, LIST = 2 } CommandType;

typedef struct {
  CommandType command_type;
  char *file_path;
} CommandPacket;

typedef struct {
  uint32_t sequence_number;
  uint32_t fragment_count;
  uint32_t data_length;
  char path[FILE_NAME_LENGTH];
  const uint8_t *data;
} DataPacket;

typedef struct {
  MessageType message_type;
  union {
    CommandPacket command_packet;
    DataPacket data_packet;
  } data;
} Packet;

typedef enum {
  PACKET_SEND_SOCKET_UNAVAILABLE = -1,
  PACKET_SEND_OK = 0,
  PACKET_SEND_SOCKET_CLOSED = 1,
} PacketSendResult;

PacketSendResult send_packet(int file_descriptor, Packet packet);
