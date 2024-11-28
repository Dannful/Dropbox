#ifndef ELECTION_H
#define ELECTION_H

#include <stdint.h>

typedef enum { AWAIT = 0, BEGIN_ELECTION = 1 } HeartbeatResult;

void send_election_message(uint8_t is_election_over, uint8_t elected);
HeartbeatResult send_heartbeat_message();
void receive_election_message(uint8_t is_election_over,
                              uint8_t current_elected);
uint8_t *get_primary_server();
void set_primary_server(uint8_t replica_id);

#endif
