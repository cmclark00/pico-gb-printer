#include "websocket_server.h"
#include "tusb_lwip_glue.h"
#include "lwip/tcp.h"
#include "lwip/pbuf.h"
#include "lwip/mem.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

// WebSocket magic string for handshake
#define WS_MAGIC_STRING "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define WS_LISTENING_PORT 8080

// Base64 encoding table
static const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Global WebSocket state
static struct tcp_pcb *ws_listening_pcb = NULL;
static ws_connection_t *ws_connections = NULL;
static uint32_t connection_count = 0;

// Simple SHA1 implementation for WebSocket handshake
static void sha1_hash(const uint8_t *data, size_t len, uint8_t hash[20]);
static void base64_encode(const uint8_t *input, size_t length, char *output);
static int strncasecmp_local(const char *s1, const char *s2, size_t n);
static char *strstr_case_insensitive(const char *haystack, const char *needle);

// WebSocket frame handling
static err_t ws_parse_frame(struct pbuf *p, ws_frame_t *frame);
static err_t ws_send_frame(ws_connection_t *conn, uint8_t opcode, const uint8_t *payload, size_t length);
static void ws_handle_frame(ws_connection_t *conn, ws_frame_t *frame);

// Connection management
static ws_connection_t *ws_create_connection(struct tcp_pcb *pcb);
static void ws_remove_connection(ws_connection_t *conn);
static err_t ws_perform_handshake(ws_connection_t *conn, struct pbuf *p);

// TCP callbacks
static err_t ws_accept_callback(void *arg, struct tcp_pcb *newpcb, err_t err);
static err_t ws_recv_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
static void ws_error_callback(void *arg, err_t err);
static err_t ws_close_callback(void *arg, struct tcp_pcb *tpcb);

// Forward declaration for sys_now (defined in tusb_lwip_glue.c)
extern uint32_t sys_now(void);

void websocket_server_init(void) {
    // Create listening PCB
    ws_listening_pcb = tcp_new();
    if (ws_listening_pcb == NULL) {
        printf("WebSocket: Failed to create listening PCB\n");
        return;
    }
    
    // Bind to port
    err_t err = tcp_bind(ws_listening_pcb, IP_ADDR_ANY, WS_LISTENING_PORT);
    if (err != ERR_OK) {
        printf("WebSocket: Failed to bind to port %d\n", WS_LISTENING_PORT);
        tcp_close(ws_listening_pcb);
        return;
    }
    
    // Start listening
    ws_listening_pcb = tcp_listen(ws_listening_pcb);
    tcp_accept(ws_listening_pcb, ws_accept_callback);
    
    printf("WebSocket server listening on port %d\n", WS_LISTENING_PORT);
}

void websocket_server_process(void) {
    // Process ping/pong and cleanup
    uint32_t current_time = sys_now();
    ws_connection_t *conn = ws_connections;
    ws_connection_t *prev = NULL;
    
    while (conn != NULL) {
        ws_connection_t *next = conn->next;
        
        // Send ping every 30 seconds
        if (conn->state == WS_STATE_CONNECTED && 
            current_time - conn->last_ping_time > 30000) {
            ws_send_frame(conn, WS_OPCODE_PING, NULL, 0);
            conn->last_ping_time = current_time;
            conn->ping_pending = true;
        }
        
        // Check for ping timeout (60 seconds)
        if (conn->ping_pending && 
            current_time - conn->last_ping_time > 60000) {
            printf("WebSocket: Ping timeout, closing connection\n");
            websocket_close_connection(conn);
            if (prev) {
                prev->next = next;
            } else {
                ws_connections = next;
            }
            conn = next;
            continue;
        }
        
        prev = conn;
        conn = next;
    }
}

static err_t ws_accept_callback(void *arg, struct tcp_pcb *newpcb, err_t err) {
    (void)arg;
    
    if (err != ERR_OK || newpcb == NULL) {
        return ERR_VAL;
    }
    
    // Create new WebSocket connection
    ws_connection_t *conn = ws_create_connection(newpcb);
    if (conn == NULL) {
        tcp_close(newpcb);
        return ERR_MEM;
    }
    
    // Set up TCP callbacks
    tcp_arg(newpcb, conn);
    tcp_recv(newpcb, ws_recv_callback);
    tcp_err(newpcb, ws_error_callback);
    tcp_poll(newpcb, ws_close_callback, 10);
    
    printf("WebSocket: New connection accepted\n");
    return ERR_OK;
}

static err_t ws_recv_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    ws_connection_t *conn = (ws_connection_t *)arg;
    
    if (err != ERR_OK || conn == NULL) {
        if (p != NULL) pbuf_free(p);
        return ERR_ABRT;
    }
    
    // Connection closed by remote
    if (p == NULL) {
        websocket_close_connection(conn);
        return ERR_OK;
    }
    
    tcp_recved(tpcb, p->tot_len);
    
    if (conn->state == WS_STATE_HANDSHAKE) {
        // Handle WebSocket handshake
        err_t result = ws_perform_handshake(conn, p);
        pbuf_free(p);
        return result;
    } else if (conn->state == WS_STATE_CONNECTED) {
        // Parse WebSocket frame
        ws_frame_t frame;
        if (ws_parse_frame(p, &frame) == ERR_OK) {
            ws_handle_frame(conn, &frame);
        }
        pbuf_free(p);
    } else {
        pbuf_free(p);
    }
    
    return ERR_OK;
}

static void ws_error_callback(void *arg, err_t err) {
    ws_connection_t *conn = (ws_connection_t *)arg;
    printf("WebSocket: TCP error %d\n", err);
    if (conn) {
        ws_remove_connection(conn);
    }
}

static err_t ws_close_callback(void *arg, struct tcp_pcb *tpcb) {
    ws_connection_t *conn = (ws_connection_t *)arg;
    printf("WebSocket: Connection closed\n");
    if (conn) {
        websocket_close_connection(conn);
    }
    return ERR_OK;
}

static ws_connection_t *ws_create_connection(struct tcp_pcb *pcb) {
    ws_connection_t *conn = (ws_connection_t *)malloc(sizeof(ws_connection_t));
    if (conn == NULL) {
        return NULL;
    }
    
    memset(conn, 0, sizeof(ws_connection_t));
    conn->pcb = pcb;
    conn->state = WS_STATE_HANDSHAKE;
    conn->last_ping_time = sys_now();
    
    // Add to connections list
    conn->next = ws_connections;
    ws_connections = conn;
    connection_count++;
    
    return conn;
}

static void ws_remove_connection(ws_connection_t *conn) {
    if (conn == NULL) return;
    
    // Remove from list
    if (ws_connections == conn) {
        ws_connections = conn->next;
    } else {
        ws_connection_t *current = ws_connections;
        while (current && current->next != conn) {
            current = current->next;
        }
        if (current) {
            current->next = conn->next;
        }
    }
    
    connection_count--;
    free(conn);
}

static err_t ws_perform_handshake(ws_connection_t *conn, struct pbuf *p) {
    // Extract HTTP request
    char *request = (char *)malloc(p->tot_len + 1);
    if (request == NULL) {
        return ERR_MEM;
    }
    
    pbuf_copy_partial(p, request, p->tot_len, 0);
    request[p->tot_len] = '\0';
    
    // Find WebSocket key
    char *key_header = strstr_case_insensitive(request, "Sec-WebSocket-Key:");
    if (key_header == NULL) {
        free(request);
        return ERR_VAL;
    }
    
    // Extract key value
    key_header += 18; // Skip "Sec-WebSocket-Key:"
    while (*key_header == ' ') key_header++; // Skip spaces
    
    char *key_end = strstr(key_header, "\r\n");
    if (key_end == NULL) {
        free(request);
        return ERR_VAL;
    }
    
    size_t key_len = key_end - key_header;
    char key[256];
    if (key_len >= sizeof(key)) {
        free(request);
        return ERR_VAL;
    }
    
    strncpy(key, key_header, key_len);
    key[key_len] = '\0';
    
    // Create accept key
    char accept_string[256];
    snprintf(accept_string, sizeof(accept_string), "%s%s", key, WS_MAGIC_STRING);
    
    uint8_t sha1_hash_result[20];
    sha1_hash((uint8_t *)accept_string, strlen(accept_string), sha1_hash_result);
    
    char accept_key[32];
    base64_encode(sha1_hash_result, 20, accept_key);
    
    // Send handshake response
    char response[512];
    int response_len = snprintf(response, sizeof(response),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n"
        "\r\n", accept_key);
    
    err_t err = tcp_write(conn->pcb, response, response_len, TCP_WRITE_FLAG_COPY);
    if (err == ERR_OK) {
        tcp_output(conn->pcb);
        conn->state = WS_STATE_CONNECTED;
        printf("WebSocket: Handshake completed\n");
    }
    
    free(request);
    return err;
}

static err_t ws_parse_frame(struct pbuf *p, ws_frame_t *frame) {
    if (p->tot_len < 2) {
        return ERR_VAL;
    }
    
    uint8_t *data = (uint8_t *)p->payload;
    
    frame->final = (data[0] & 0x80) != 0;
    frame->opcode = data[0] & 0x0F;
    frame->masked = (data[1] & 0x80) != 0;
    
    uint8_t payload_len = data[1] & 0x7F;
    size_t header_len = 2;
    
    if (payload_len == 126) {
        if (p->tot_len < 4) return ERR_VAL;
        frame->payload_length = (data[2] << 8) | data[3];
        header_len = 4;
    } else if (payload_len == 127) {
        if (p->tot_len < 10) return ERR_VAL;
        // For simplicity, we don't support 64-bit lengths
        return ERR_VAL;
    } else {
        frame->payload_length = payload_len;
    }
    
    if (frame->masked) {
        if (p->tot_len < header_len + 4) return ERR_VAL;
        frame->mask_key = *(uint32_t *)(data + header_len);
        header_len += 4;
    }
    
    if (p->tot_len < header_len + frame->payload_length) {
        return ERR_VAL;
    }
    
    frame->payload = data + header_len;
    return ERR_OK;
}

static void ws_handle_frame(ws_connection_t *conn, ws_frame_t *frame) {
    switch (frame->opcode) {
        case WS_OPCODE_CLOSE:
            websocket_close_connection(conn);
            break;
            
        case WS_OPCODE_PING:
            ws_send_frame(conn, WS_OPCODE_PONG, frame->payload, frame->payload_length);
            break;
            
        case WS_OPCODE_PONG:
            conn->ping_pending = false;
            break;
            
        case WS_OPCODE_TEXT:
            // Echo back for now (can be customized)
            printf("WebSocket: Received text: %.*s\n", (int)frame->payload_length, frame->payload);
            break;
            
        default:
            // Ignore unknown opcodes
            break;
    }
}

static err_t ws_send_frame(ws_connection_t *conn, uint8_t opcode, const uint8_t *payload, size_t length) {
    if (conn->state != WS_STATE_CONNECTED) {
        return ERR_CONN;
    }
    
    size_t header_len = 2;
    if (length > 65535) {
        return ERR_VAL; // Too long for our simple implementation
    } else if (length > 125) {
        header_len = 4;
    }
    
    uint8_t *frame = (uint8_t *)malloc(header_len + length);
    if (frame == NULL) {
        return ERR_MEM;
    }
    
    // Build frame header
    frame[0] = 0x80 | opcode; // FIN bit + opcode
    
    if (length > 125) {
        frame[1] = 126;
        frame[2] = (length >> 8) & 0xFF;
        frame[3] = length & 0xFF;
    } else {
        frame[1] = length & 0x7F;
    }
    
    // Copy payload
    if (payload && length > 0) {
        memcpy(frame + header_len, payload, length);
    }
    
    err_t err = tcp_write(conn->pcb, frame, header_len + length, TCP_WRITE_FLAG_COPY);
    if (err == ERR_OK) {
        tcp_output(conn->pcb);
    }
    
    free(frame);
    return err;
}

void websocket_close_connection(ws_connection_t *conn) {
    if (conn == NULL) return;
    
    if (conn->state == WS_STATE_CONNECTED) {
        ws_send_frame(conn, WS_OPCODE_CLOSE, NULL, 0);
    }
    
    if (conn->pcb) {
        tcp_close(conn->pcb);
        conn->pcb = NULL;
    }
    
    conn->state = WS_STATE_DISCONNECTED;
    ws_remove_connection(conn);
}

void websocket_broadcast_text(const char *message) {
    if (message == NULL) return;
    
    size_t len = strlen(message);
    ws_connection_t *conn = ws_connections;
    
    while (conn != NULL) {
        if (conn->state == WS_STATE_CONNECTED) {
            ws_send_frame(conn, WS_OPCODE_TEXT, (const uint8_t *)message, len);
        }
        conn = conn->next;
    }
}

void websocket_broadcast_binary(const uint8_t *data, size_t length) {
    ws_connection_t *conn = ws_connections;
    
    while (conn != NULL) {
        if (conn->state == WS_STATE_CONNECTED) {
            ws_send_frame(conn, WS_OPCODE_BINARY, data, length);
        }
        conn = conn->next;
    }
}

void websocket_send_text(ws_connection_t *conn, const char *message) {
    if (message == NULL) return;
    ws_send_frame(conn, WS_OPCODE_TEXT, (const uint8_t *)message, strlen(message));
}

void websocket_send_binary(ws_connection_t *conn, const uint8_t *data, size_t length) {
    ws_send_frame(conn, WS_OPCODE_BINARY, data, length);
}

size_t websocket_get_connection_count(void) {
    return connection_count;
}

// Real-time trading event broadcasting functions
void websocket_broadcast_trade_event(const char *event_type, const char *message) {
    char json_buffer[512];
    snprintf(json_buffer, sizeof(json_buffer),
        "{\"type\":\"trade_event\",\"event\":\"%s\",\"message\":\"%s\",\"timestamp\":%lu}",
        event_type, message, sys_now());
    websocket_broadcast_text(json_buffer);
}

void websocket_broadcast_protocol_data(uint8_t rx_byte, uint8_t tx_byte, const char *state) {
    char json_buffer[256];
    snprintf(json_buffer, sizeof(json_buffer),
        "{\"type\":\"protocol\",\"rx\":\"0x%02X\",\"tx\":\"0x%02X\",\"state\":\"%s\",\"timestamp\":%lu}",
        rx_byte, tx_byte, state, sys_now());
    websocket_broadcast_text(json_buffer);
}

void websocket_broadcast_pokemon_data(const char *pokemon_json) {
    char json_buffer[1024];
    snprintf(json_buffer, sizeof(json_buffer),
        "{\"type\":\"pokemon_update\",\"data\":%s,\"timestamp\":%lu}",
        pokemon_json, sys_now());
    websocket_broadcast_text(json_buffer);
}

void websocket_broadcast_status_update(const char *status_json) {
    char json_buffer[1024];
    snprintf(json_buffer, sizeof(json_buffer),
        "{\"type\":\"status_update\",\"data\":%s,\"timestamp\":%lu}",
        status_json, sys_now());
    websocket_broadcast_text(json_buffer);
}

// Utility functions
static int strncasecmp_local(const char *s1, const char *s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char c1 = s1[i];
        char c2 = s2[i];
        
        // Convert to lowercase
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        
        if (c1 != c2) return c1 - c2;
        if (c1 == '\0') return 0;
    }
    return 0;
}

static char *strstr_case_insensitive(const char *haystack, const char *needle) {
    if (!needle || !*needle) return (char *)haystack;
    
    size_t needle_len = strlen(needle);
    for (const char *p = haystack; *p; p++) {
        if (strncasecmp_local(p, needle, needle_len) == 0) {
            return (char *)p;
        }
    }
    return NULL;
}

static void base64_encode(const uint8_t *input, size_t length, char *output) {
    size_t i, j;
    for (i = 0, j = 0; i < length; i += 3, j += 4) {
        uint32_t triple = (input[i] << 16) | 
                         ((i + 1 < length) ? (input[i + 1] << 8) : 0) |
                         ((i + 2 < length) ? input[i + 2] : 0);
        
        output[j] = base64_table[(triple >> 18) & 0x3F];
        output[j + 1] = base64_table[(triple >> 12) & 0x3F];
        output[j + 2] = (i + 1 < length) ? base64_table[(triple >> 6) & 0x3F] : '=';
        output[j + 3] = (i + 2 < length) ? base64_table[triple & 0x3F] : '=';
    }
    output[j] = '\0';
}

// Proper SHA1 implementation for WebSocket handshake
static void sha1_hash(const uint8_t *data, size_t len, uint8_t hash[20]) {
    // SHA1 implementation based on RFC 3174
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    uint32_t w[80];
    uint64_t total_len = len * 8;
    
    // Process message in successive 512-bit chunks
    size_t chunk_len = len;
    uint8_t *padded_data = malloc(((len + 8) / 64 + 1) * 64);
    if (!padded_data) return;
    
    memcpy(padded_data, data, len);
    
    // Append single '1' bit
    padded_data[len] = 0x80;
    
    // Pad with zeros
    size_t padded_len = ((len + 8) / 64 + 1) * 64;
    memset(padded_data + len + 1, 0, padded_len - len - 1);
    
    // Append length as 64-bit big-endian
    for (int i = 0; i < 8; i++) {
        padded_data[padded_len - 8 + i] = (total_len >> (8 * (7 - i))) & 0xFF;
    }
    
    // Process each 512-bit chunk
    for (size_t chunk_start = 0; chunk_start < padded_len; chunk_start += 64) {
        // Break chunk into sixteen 32-bit words
        for (int i = 0; i < 16; i++) {
            w[i] = (padded_data[chunk_start + i * 4] << 24) |
                   (padded_data[chunk_start + i * 4 + 1] << 16) |
                   (padded_data[chunk_start + i * 4 + 2] << 8) |
                   (padded_data[chunk_start + i * 4 + 3]);
        }
        
        // Extend the sixteen 32-bit words into eighty 32-bit words
        for (int i = 16; i < 80; i++) {
            uint32_t temp = w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16];
            w[i] = (temp << 1) | (temp >> 31);
        }
        
        // Initialize hash value for this chunk
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        
        // Main loop
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            
            uint32_t temp = ((a << 5) | (a >> 27)) + f + e + k + w[i];
            e = d;
            d = c;
            c = (b << 30) | (b >> 2);
            b = a;
            a = temp;
        }
        
        // Add this chunk's hash to result
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }
    
    // Produce final hash value as 160-bit number (20 bytes)
    for (int i = 0; i < 5; i++) {
        hash[i * 4] = (h[i] >> 24) & 0xFF;
        hash[i * 4 + 1] = (h[i] >> 16) & 0xFF;
        hash[i * 4 + 2] = (h[i] >> 8) & 0xFF;
        hash[i * 4 + 3] = h[i] & 0xFF;
    }
    
    free(padded_data);
} 