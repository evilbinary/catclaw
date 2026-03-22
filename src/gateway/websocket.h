#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <stdbool.h>
#include <stdint.h>

// WebSocket connection structure
typedef struct {
    int socket;
    bool handshake_completed;
    bool authorized;
    char buffer[4096];
    int buffer_len;
} WebSocketConnection;

// WebSocket server structure
typedef struct {
    int server_socket;
    int port;
    bool running;
    WebSocketConnection *connections;
    int connection_count;
    int max_connections;
    char* api_key;                      // API 授权密钥
} WebSocketServer;

// Functions
bool websocket_server_init(WebSocketServer *server, int port, int max_connections, const char* api_key);
bool websocket_server_start(WebSocketServer *server);
void websocket_server_stop(WebSocketServer *server);
void websocket_server_cleanup(WebSocketServer *server);
bool websocket_send(WebSocketConnection *conn, const char *message);

#endif // WEBSOCKET_H
