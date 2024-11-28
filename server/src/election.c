#include "../include/election.h"
#include "../../core/connection.h"
#include "../../core/writer.h"
#include "../include/connection.h"
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

uint8_t *in_election = NULL;

void send_election_message(uint8_t is_election_over, uint8_t elected) {
  uint8_t neighbour = (get_replica_id() + 1) % get_number_of_replicas();
  int connection_fd = -1;
  ServerReplica *replica = get_server_replica(neighbour);
  ConnectionResult result =
      open_connection(&connection_fd, replica->hostname, replica->port);
  if (result != SERVER_CONNECTION_SUCCESS) {
    printf("Failed to send election message to neighbour %d\n.", neighbour);
    return;
  }
  Packet election_packet;
  election_packet.type = ELECTION;
  election_packet.sequence_number = 0;
  election_packet.total_size = 1;

  Writer *writer = create_writer();
  write_bytes(writer, &is_election_over, sizeof(uint8_t));
  write_bytes(writer, &elected, sizeof(uint8_t));
  election_packet.length = writer->length;

  if (send(connection_fd, &election_packet, sizeof(Packet), 0) == 0) {
    destroy_writer(writer);
    printf("Failed to send election message to neighbour %d\n.", neighbour);
    return;
  }
  if (send(connection_fd, writer->buffer, writer->length, 0) == 0) {
    destroy_writer(writer);
    printf("Failed to send election message to neighbour %d\n.", neighbour);
    return;
  }
  destroy_writer(writer);

  Packet response_packet;
  if (safe_recv(connection_fd, &response_packet, sizeof(Packet), 0) !=
      sizeof(Packet)) {
    printf("Failed to receive ACK message for election from neighbour %d\n.",
           neighbour);
    return;
  }

  if (memcmp(&response_packet, &election_packet, sizeof(Packet)) != 0) {
    printf("Response received from neighbour %d differs from original "
           "packet.\n",
           neighbour);
    return;
  }
  close(connection_fd);
}

HeartbeatResult send_heartbeat_message() {
  uint8_t neighbour = (get_replica_id() + 1) % get_number_of_replicas();
  int connection_fd = -1;
  ServerReplica *replica = get_server_replica(neighbour);
  ConnectionResult result =
      open_connection(&connection_fd, replica->hostname, replica->port);
  if (result != SERVER_CONNECTION_SUCCESS)
    return SERVER_DEAD;
  Packet heartbeat_packet;
  heartbeat_packet.type = HEARTBEAT;
  heartbeat_packet.sequence_number = 0;
  heartbeat_packet.total_size = 1;
  heartbeat_packet.length = 0;
  if (send(connection_fd, &heartbeat_packet, sizeof(Packet), 0) <= 0)
    return SERVER_DEAD;
  Packet response_packet;
  if (safe_recv(connection_fd, &response_packet, sizeof(Packet), 0) !=
      sizeof(Packet))
    return SERVER_DEAD;
  close(connection_fd);
  return SERVER_ALIVE;
}

void receive_election_message(uint8_t is_election_over,
                              uint8_t current_elected) {
  uint8_t this_replica_id = get_replica_id();
  if (in_election == NULL)
    in_election = calloc(sizeof(uint8_t), get_number_of_replicas());
  if (is_election_over && current_elected == this_replica_id) {
    free(in_election);
    in_election = NULL;
    return;
  }
  uint8_t source =
      this_replica_id == 0 ? get_number_of_replicas() - 1 : this_replica_id - 1;
  if (in_election[source])
    return;
  in_election[source] = 1;
  send_election_message(is_election_over, get_replica_id() > current_elected
                                              ? get_replica_id()
                                              : current_elected);
}
