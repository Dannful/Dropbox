#ifndef ELECTION_H
#define ELECTION_H

#include <stdint.h>

typedef enum { SERVER_ALIVE = 0, SERVER_DEAD = 1 } HeartbeatResult;

void send_election_message(uint8_t elected);
HeartbeatResult send_heartbeat_message();
void receive_election_message(uint8_t is_election_over,
                              uint8_t current_elected);

#endif
