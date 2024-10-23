#include "./packet.h"
#include <sys/socket.h>

PacketSendResult send_packet(int socket_file_descriptor, Packet packet) {
  if (socket_file_descriptor == -1) {
    return PACKET_SEND_SOCKET_UNAVAILABLE;
  }

  if (send(socket_file_descriptor, &packet.message_type,
           sizeof(packet.message_type), 0) == 0)
    return PACKET_SEND_SOCKET_CLOSED;

  switch (packet.message_type) {
  case COMMAND:
    if (send(socket_file_descriptor, &packet.data.command_packet,
             sizeof(CommandPacket), 0) == 0)
      return PACKET_SEND_SOCKET_CLOSED;
    break;
  case DATA:
    if (send(socket_file_descriptor, &packet.data.data_packet,
             sizeof(packet.data.data_packet) -
                 sizeof(packet.data.data_packet.data),
             0) == 0 ||
        send(socket_file_descriptor, packet.data.data_packet.data,
             packet.data.data_packet.data_length, 0) == 0)
      return PACKET_SEND_SOCKET_CLOSED;
    break;
  }
  return PACKET_SEND_OK;
}
