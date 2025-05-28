#ifndef WEBSOCKET_SERVER_H
#define WEBSOCKET_SERVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "lwip/err.h"
#include "lwip/tcp.h"
#include "lwip/pbuf.h"

// WebSocket frame opcodes
#define WS_OPCODE_CONTINUATION 0x00
#define WS_OPCODE_TEXT         0x01
#define WS_OPCODE_BINARY       0x02
#define WS_OPCODE_CLOSE        0x08
#define WS_OPCODE_PING         0x09
#define WS_OPCODE_PONG         0x0A

// WebSocket connection state
typedef enum {
    WS_STATE_DISCONNECTED,
    WS_STATE_HANDSHAKE,
    WS_STATE_CONNECTED,
    WS_STATE_CLOSING
} ws_state_t;

// WebSocket connection structure
typedef struct ws_connection {
    struct tcp_pcb *pcb;
    ws_state_t state;
    uint32_t last_ping_time;
    bool ping_pending;
    struct ws_connection *next;
} ws_connection_t;

// WebSocket message structure
typedef struct {
    uint8_t opcode;
    bool final;
    bool masked;
    uint64_t payload_length;
    uint32_t mask_key;
    uint8_t *payload;
} ws_frame_t;

// Function declarations
void websocket_server_init(void);
void websocket_server_process(void);
err_t websocket_accept_connection(struct tcp_pcb *pcb, struct pbuf *p);
void websocket_broadcast_text(const char *message);
void websocket_broadcast_binary(const uint8_t *data, size_t length);
void websocket_send_text(ws_connection_t *conn, const char *message);
void websocket_send_binary(ws_connection_t *conn, const uint8_t *data, size_t length);
void websocket_close_connection(ws_connection_t *conn);
size_t websocket_get_connection_count(void);

// Real-time trading event broadcasting
void websocket_broadcast_trade_event(const char *event_type, const char *message);
void websocket_broadcast_protocol_data(uint8_t rx_byte, uint8_t tx_byte, const char *state);
void websocket_broadcast_pokemon_data(const char *pokemon_json);
void websocket_broadcast_status_update(const char *status_json);

#endif // WEBSOCKET_SERVER_H 