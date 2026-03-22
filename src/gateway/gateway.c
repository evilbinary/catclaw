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
    g_gateway.ws_initialized = false;

    // Initialize WebSocket server only if enabled
    if (g_config.gateway.websocket_enabled) {
        if (!websocket_server_init(&g_gateway.ws_server, g_gateway.port, 10, g_config.gateway.http_api_key)) {
            fprintf(stderr, "Failed to initialize WebSocket server\n");
            return false;
        }
        g_gateway.ws_initialized = true;
        printf("WebSocket server initialized on port %d\n", g_gateway.port);
    }

    printf("Gateway initialized\n");
    return true;
}

bool gateway_start(void) {
    if (g_gateway.running) {
        printf("Gateway is already running\n");
        return true;
    }

    // Start WebSocket server only if enabled and initialized
    if (g_config.gateway.websocket_enabled && g_gateway.ws_initialized) {
        if (!websocket_server_start(&g_gateway.ws_server)) {
            fprintf(stderr, "Failed to start WebSocket server\n");
            return false;
        }
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

    // Stop WebSocket server only if initialized
    if (g_gateway.ws_initialized) {
        websocket_server_stop(&g_gateway.ws_server);
    }
    g_gateway.running = false;
    printf("Gateway stopped\n");
}

void gateway_cleanup(void) {
    if (g_gateway.running) {
        gateway_stop();
    }

    // Cleanup WebSocket server only if initialized
    if (g_gateway.ws_initialized) {
        websocket_server_cleanup(&g_gateway.ws_server);
    }
    printf("Gateway cleaned up\n");
}

void gateway_status(void) {
    printf("Gateway:\n");
    printf("  Status: %s\n", g_gateway.running ? "running" : "stopped");
    printf("  Port: %d\n", g_gateway.port);
    printf("  HTTP Server: %s\n", g_config.gateway.http_server_enabled ? "enabled" : "disabled");
    printf("  WebSocket: %s\n", g_config.gateway.websocket_enabled ? "enabled" : "disabled");
    printf("  Bind: 127.0.0.1\n");
}

// Broadcast a message to all connected WebSocket clients
bool gateway_broadcast_to_webchat(const char *message) {
    // Skip if websocket is not enabled or not initialized
    if (!g_config.gateway.websocket_enabled || !g_gateway.ws_initialized) {
        return true;
    }
    
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
