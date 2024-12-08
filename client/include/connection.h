#ifndef CONNECTION_H
#define CONNECTION_H

#include "../../core/connection.h"
#include "../../core/packet.h"
#include "../../core/reader.h"
#include <stdint.h>

void set_server_data(char host[], uint16_t port);
ConnectionResult open_control_connection();
void *connection_handler(void *arg);
ServerBindResult setup_reverse_connection_listener(uint16_t port);
void *handle_reconnect(void *arg);
void *control_connection_handler(void *arg);
void *download_connection_handler(void *arg);
void *check_connection_handler(void *arg);

void send_upload_message(char path[]);
void send_download_message(char path[], uint8_t sync);
void send_delete_message(char path[]);
void send_list_server_message();
void send_sync_dir_message();
void close_connection();
void close_reverse_connection();
void set_username(char user[USERNAME_LENGTH]);
void *pooling_manager(void *arg);
uint8_t decode_file(Reader *reader, Packet packet);
#endif
