#include "gateway.h"
#include "common/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global gateway instance
Gateway g_gateway = {
    .port = 0,
    .running = false
};

bool gateway_init(void) {
    g_gateway.port = g_config.gateway_port;
    g_gateway.running = false;

    // Initialize WebSocket server
    if (!websocket_server_init(&g_gateway.ws_server, g_gateway.port, 10, g_config.gateway.http_api_key)) {
        fprintf(stderr, "Failed to initialize WebSocket server\n");
        return false;
    }

    printf("Gateway initialized on port %d\n", g_gateway.port);
    return true;
}

bool gateway_start(void) {
    if (g_gateway.running) {
        printf("Gateway is already running\n");
        return true;
    }

    // Start WebSocket server
    if (!websocket_server_start(&g_gateway.ws_server)) {
        fprintf(stderr, "Failed to start WebSocket server\n");
        return false;
    }

    g_gateway.running = true;
    printf("Gateway started on port %d\n", g_gateway.port);
    return true;
}

void gateway_stop(void) {
    if (!g_gateway.running) {
        printf("Gateway is not running\n");
        return;
    }

    // Stop WebSocket server
    websocket_server_stop(&g_gateway.ws_server);
    g_gateway.running = false;
    printf("Gateway stopped\n");
}

void gateway_cleanup(void) {
    if (g_gateway.running) {
        gateway_stop();
    }

    // Cleanup WebSocket server
    websocket_server_cleanup(&g_gateway.ws_server);
    printf("Gateway cleaned up\n");
}

void gateway_status(void) {
    printf("Gateway:\n");
    printf("  Status: %s\n", g_gateway.running ? "running" : "stopped");
    printf("  Port: %d\n", g_gateway.port);
    printf("  Bind: 127.0.0.1\n");
}

// Broadcast a message to all connected WebSocket clients
bool gateway_broadcast_to_webchat(const char *message) {
    if (!message || strlen(message) == 0) return true;

    WebSocketServer *server = &g_gateway.ws_server;
    if (!server->connections) return false;

    bool all_success = true;
    for (int i = 0; i < server->max_connections; i++) {
        if (server->connections[i].socket != 0 && server->connections[i].handshake_completed) {
            if (!websocket_send(&server->connections[i], message)) {
                all_success = false;
            }
        }
    }
    return all_success;
}
