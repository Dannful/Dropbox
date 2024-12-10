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

uint8_t get_next_neighbour(uint8_t server_id, uint8_t get_dead, uint8_t get_primary) {
  uint8_t replicas = get_number_of_replicas();
  uint8_t result = get_replica_id();
  do {
    if((get_dead || !dead[result]) && result != get_replica_id() && (get_primary || *get_primary_server() != result))
      return result;
    result = (result + 1) % replicas;
  } while(result != get_replica_id());
  return result;
}

uint8_t get_previous_neighbour(uint8_t server_id) {
  uint8_t replicas = get_number_of_replicas();
  uint8_t previous = server_id == 0 ? replicas - 1 : server_id - 1;
  if(primary_server != NULL && *primary_server == previous || dead[previous])
    return previous == 0 ? replicas - 1 : previous - 1;
  return previous;
}

void send_election_message(uint8_t is_election_over, uint8_t elected) {
  uint8_t neighbour = get_next_neighbour(get_replica_id(), 0, 1);
  if(neighbour == get_replica_id())
    return;
  neighbour = get_next_neighbour(get_replica_id(), 0, 0);
  if(neighbour == get_replica_id()) {
    printf("Nobody to send election message to.\n");
    return;
  }
  int connection_fd = -1;
  ServerReplica *replica = get_server_replica(neighbour);
  ConnectionResult result =
      open_connection(&connection_fd, replica->hostname, replica->port);
  printf("Sending ELECTION message to %d. Election over: %d. Elected: %d.\n", neighbour, is_election_over, elected);
  if (result != SERVER_CONNECTION_SUCCESS) {
    printf("Failed to send election message to neighbour %d. IP: %s:%d\n", neighbour, replica->hostname, replica->port);
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
    close(connection_fd);
    destroy_writer(writer);
    printf("Failed to send election message to neighbour %d\n.", neighbour);
    return;
  }
  if (send(connection_fd, writer->buffer, writer->length, 0) == 0) {
    close(connection_fd);
    destroy_writer(writer);
    printf("Failed to send election message to neighbour %d.\n", neighbour);
    return;
  }
  destroy_writer(writer);

  close(connection_fd);
}

HeartbeatResult send_heartbeat_message() {
  if (primary_server == NULL || in_election != NULL)
    return AWAIT;
  uint8_t id = get_replica_id();
  uint8_t primary_dead = 0;
  uint8_t all_dead = 1;
  for(int neighbour = 0; neighbour < get_number_of_replicas(); neighbour++) {
    if(neighbour == id)
      continue;
    if(!dead[neighbour])
      all_dead = 0;
    int connection_fd = -1;
    ServerReplica *replica = get_server_replica(neighbour);
    printf("Sending HEARTBEAT message to %d at %s:%d...\n", neighbour, replica->hostname, replica->port);
    ConnectionResult result =
        open_connection(&connection_fd, replica->hostname, replica->port);
    if (result != SERVER_CONNECTION_SUCCESS) {
      dead[neighbour] = 1;
      primary_dead = *get_primary_server() == neighbour;
      continue;
    }
    Packet heartbeat_packet;
    heartbeat_packet.type = HEARTBEAT;
    heartbeat_packet.sequence_number = 0;
    heartbeat_packet.total_size = 1;
    uint8_t response_needed = 1;
    Writer *writer = create_writer();
    write_bytes(writer, &response_needed, sizeof(response_needed));
    write_bytes(writer, &id, sizeof(id));
    heartbeat_packet.length = writer->length;
    if (send(connection_fd, &heartbeat_packet, sizeof(Packet), 0) <= 0) {
      close(connection_fd);
      destroy_writer(writer);
      dead[neighbour] = 1;
      continue;
    }
    if (send(connection_fd, writer->buffer, writer->length, 0) <= 0) {
      close(connection_fd);
      destroy_writer(writer);
      dead[neighbour] = 1;
      continue;
    }
    Packet response_packet;
    if (safe_recv(connection_fd, &response_packet, sizeof(Packet), 0) !=
        sizeof(Packet)) {
      close(connection_fd);
      destroy_writer(writer);
      dead[neighbour] = 1;
      continue;
    }
    close(connection_fd);
    destroy_writer(writer);
    printf("Received HEARTBEAT back from %d. Everything's good.\n", neighbour);
  }
  if(all_dead) {
    printf("Everyone is dead, so I guess I'm the new primary server.\n");
    set_primary_server(get_replica_id());
  }
  return primary_dead && !all_dead ? BEGIN_ELECTION : AWAIT;
}

void receive_election_message(uint8_t is_election_over,
                              uint8_t current_elected, uint8_t *unalive) {

  uint8_t this_replica_id = get_replica_id();
  uint8_t source = get_previous_neighbour(this_replica_id);
  if (in_election == NULL)
    in_election = calloc(sizeof(uint8_t), get_number_of_replicas());
  for(int i = 0; i < get_number_of_replicas(); i++)
    dead[i] = dead[i] | unalive[i];
  is_election_over = is_election_over || current_elected == this_replica_id;
  if (in_election[source] && !is_election_over)
    return;
  in_election[source] = 1;
  if (is_election_over) {
    if(current_elected == this_replica_id && *get_primary_server() == this_replica_id) {
      free(in_election);
      in_election = NULL;
      return;
    }
    set_primary_server(current_elected);
    printf("A new primary server has been elected: %d.\n", *get_primary_server());
    free(in_election);
    in_election = NULL;
  }
  send_election_message(is_election_over, get_replica_id() > current_elected
                                              ? get_replica_id()
                                              : current_elected);
}

uint8_t *get_primary_server() { return primary_server; }
void set_primary_server(uint8_t replica_id) {
  if (primary_server == NULL)
    primary_server = malloc(sizeof(uint8_t));
  *primary_server = replica_id;

  if(*primary_server == get_replica_id()) {
    printf("Am new primary.\n");
    reconnect_to_clients();
  }
}

void read_server_data_file() {
  const char *file_name = "./servers.txt";
  FILE *f = fopen(file_name, "r");
  if(f == NULL) {
    printf("Server replicas file missing. Please create one at %s.\n", file_name);
    exit(1);
    return;
  }

  char buffer[1024] = {0};

  while(fgets(buffer, sizeof(buffer), f)) {
    char *token = strtok(buffer, " ");

    ServerReplica replica;
    replica.hostname = strdup(token);

    token = strtok(NULL, " ");
    replica.port = atoi(token);

    token = strtok(NULL, " ");
    replica.id = atoi(token);

    register_server_replica(replica);
  }
  fclose(f);
}

void revive_server(uint8_t id) {
  dead[id] = 0;
}
