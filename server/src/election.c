#include "../include/election.h"
#include "../../core/connection.h"
#include "../../core/writer.h"
#include "../include/connection.h"
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <stdlib.h>

uint8_t *in_election = NULL;
uint8_t *primary_server = NULL;
uint8_t *dead = NULL;

uint8_t get_next_neighbour(uint8_t server_id) {
  uint8_t replicas = get_number_of_replicas();
  uint8_t next = (server_id + 1) % replicas;
  if(primary_server != NULL && next == *primary_server || dead[next])
    return (next + 1) % replicas;
  return next;
}

uint8_t get_previous_neighbour(uint8_t server_id) {
  uint8_t replicas = get_number_of_replicas();
  uint8_t previous = server_id == 0 ? replicas - 1 : server_id - 1;
  if(primary_server != NULL && *primary_server == previous || dead[previous])
    return previous == 0 ? replicas - 1 : previous - 1;
  return previous;
}

void send_election_message(uint8_t is_election_over, uint8_t elected) {
  uint8_t neighbour = get_next_neighbour(get_replica_id());
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
  write_bytes(writer, dead, sizeof(uint8_t) * get_number_of_replicas());
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
  if (primary_server == NULL || in_election != NULL)
    return AWAIT;
  uint8_t neighbour = (get_replica_id() + 1) % get_number_of_replicas();
  int connection_fd = -1;
  ServerReplica *replica = get_server_replica(neighbour);
  ConnectionResult result =
      open_connection(&connection_fd, replica->hostname, replica->port);
  if (result != SERVER_CONNECTION_SUCCESS) {
    dead[neighbour] = 1;
    return AWAIT;
  }
  Packet heartbeat_packet;
  heartbeat_packet.type = HEARTBEAT;
  heartbeat_packet.sequence_number = 0;
  heartbeat_packet.total_size = 1;
  Writer *writer = create_writer();
  write_bytes(writer, primary_server, sizeof(uint8_t));
  heartbeat_packet.length = writer->length;
  if (send(connection_fd, &heartbeat_packet, sizeof(Packet), 0) <= 0) {
    destroy_writer(writer);
    dead[neighbour] = 1;
    return AWAIT;
  }
  if (send(connection_fd, writer->buffer, writer->length, 0) <= 0) {
    destroy_writer(writer);
    dead[neighbour] = 1;
    return AWAIT;
  }
  Packet response_packet;
  if (safe_recv(connection_fd, &response_packet, sizeof(Packet), 0) !=
      sizeof(Packet)) {
    destroy_writer(writer);
    dead[neighbour] = 1;
    return AWAIT;
  }
  if (response_packet.length == 0) {
    close(connection_fd);
    destroy_writer(writer);
    return BEGIN_ELECTION;
  } else {
    uint8_t neighbour_elected;
    if (safe_recv(connection_fd, &neighbour_elected, sizeof(neighbour_elected),
                  0) != sizeof(neighbour_elected)) {
      close(connection_fd);
      destroy_writer(writer);
      dead[neighbour] = 1;
      return AWAIT;
    }
    if (*primary_server != neighbour_elected) {
      close(connection_fd);
      destroy_writer(writer);
      dead[neighbour] = 1;
      return BEGIN_ELECTION;
    }
  }
  close(connection_fd);
  destroy_writer(writer);
  dead[neighbour] = 1;
  return AWAIT;
}

void receive_election_message(uint8_t is_election_over,
                              uint8_t current_elected, uint8_t *unalive) {
  uint8_t this_replica_id = get_replica_id();
  if (in_election == NULL)
    in_election = calloc(sizeof(uint8_t), get_number_of_replicas());
  if (is_election_over && current_elected == this_replica_id) {
    free(in_election);
    in_election = NULL;
    return;
  }
  for(int i = 0; i < get_number_of_replicas(); i++)
    dead[i] = dead[i] | unalive[i];
  is_election_over = current_elected == this_replica_id;
  uint8_t source = get_previous_neighbour(this_replica_id);
  if (in_election[source])
    return;
  if (is_election_over)
    set_primary_server(current_elected);
  in_election[source] = 1;
  send_election_message(is_election_over, get_replica_id() > current_elected
                                              ? get_replica_id()
                                              : current_elected);
}

uint8_t *get_primary_server() { return primary_server; }
void set_primary_server(uint8_t replica_id) {
  if (primary_server == NULL)
    primary_server = malloc(sizeof(uint8_t));
  *primary_server = replica_id;
}
