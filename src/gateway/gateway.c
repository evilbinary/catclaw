#include "gateway.h"
#include "http_api.h"
#include "feishu_ws.h"
#include "common/config.h"
#include "common/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global gateway instance
Gateway g_gateway = {
    .port = 0,
    .running = false,
    .ws_initialized = false,
    .http_api_server = NULL,
    .http_port = 8080,
    .http_initialized = false,
    .feishu_ws_clients = NULL,
    .feishu_ws_count = 0,
    .feishu_ws_capacity = 0
};

bool gateway_init(void) {
    g_gateway.port = g_config.gateway_port;
    g_gateway.running = false;
    g_gateway.ws_initialized = false;
    g_gateway.http_initialized = false;
    g_gateway.feishu_ws_count = 0;

    // Initialize WebSocket server only if enabled
    if (g_config.gateway.websocket_enabled) {
        if (!websocket_server_init(&g_gateway.ws_server, g_gateway.port, 10, g_config.gateway.http_api_key)) {
            log_error("Failed to initialize WebSocket server");
            return false;
        }
        g_gateway.ws_initialized = true;
        log_info("WebSocket server initialized on port %d", g_gateway.port);
    }

    // Initialize HTTP API server only if enabled
    if (g_config.gateway.http_server_enabled) {
        g_gateway.http_port = g_config.gateway.http_port > 0 ? g_config.gateway.http_port : 8080;
        g_gateway.http_api_server = http_api_init(g_gateway.http_port);
        if (g_gateway.http_api_server) {
            g_gateway.http_initialized = true;
            log_info("HTTP API server initialized on port %d", g_gateway.http_port);
        } else {
            log_error("Failed to initialize HTTP API server");
            // Continue anyway, not fatal
        }
    }

    log_info("Gateway initialized");
    return true;
}

bool gateway_start(void) {
    if (g_gateway.running) {
        log_info("Gateway is already running");
        return true;
    }

    // Start WebSocket server only if enabled and initialized
    if (g_config.gateway.websocket_enabled && g_gateway.ws_initialized) {
        if (!websocket_server_start(&g_gateway.ws_server)) {
            log_error("Failed to start WebSocket server");
            return false;
        }
    }

    // Start HTTP API server only if initialized
    if (g_gateway.http_initialized && g_gateway.http_api_server) {
        if (!http_api_start(g_gateway.http_api_server)) {
            log_error("Failed to start HTTP API server");
            // Continue anyway, not fatal
        }
    }

    // Start Feishu WebSocket clients for channels with ws_enabled=true
    if (g_config.channels.count > 0) {
        // Count feishu channels with ws_enabled
        int feishu_count = 0;
        for (int i = 0; i < g_config.channels.count; i++) {
            ChannelConfigEntry *ch = &g_config.channels.channels[i];
            if (ch->type && strcmp(ch->type, "feishu") == 0 &&
                ch->ws_enabled && ch->app_id && ch->app_secret) {
                feishu_count++;
            }
        }

        if (feishu_count > 0) {
            g_gateway.feishu_ws_capacity = feishu_count;
            g_gateway.feishu_ws_clients = (FeishuWsClient**)calloc(feishu_count, sizeof(FeishuWsClient*));
            if (!g_gateway.feishu_ws_clients) {
                log_error("Failed to allocate Feishu clients array");
            } else {
                for (int i = 0; i < g_config.channels.count; i++) {
                    ChannelConfigEntry *ch = &g_config.channels.channels[i];
                    if (ch->type && strcmp(ch->type, "feishu") == 0 &&
                        ch->ws_enabled && ch->app_id && ch->app_secret) {

                        FeishuWsClient* client = feishu_ws_create(ch->app_id, ch->app_secret);
                        if (client) {
                            if (ch->ws_domain) {
                                feishu_ws_set_domain(client, ch->ws_domain);
                            }
                            if (ch->ws_ping_interval > 0) {
                                feishu_ws_set_ping_interval(client, ch->ws_ping_interval);
                            }
                            if (ch->ws_reconnect_interval > 0 || ch->ws_max_reconnect != 0) {
                                feishu_ws_set_reconnect(client,
                                    ch->ws_reconnect_interval > 0 ? ch->ws_reconnect_interval : 120,
                                    ch->ws_max_reconnect);
                            }

                            if (feishu_ws_start(client)) {
                                g_gateway.feishu_ws_clients[g_gateway.feishu_ws_count++] = client;
                                log_info("Feishu WebSocket client started for channel: %s", ch->id);
                            } else {
                                log_error("Failed to start Feishu WebSocket client for channel: %s", ch->id);
                                feishu_ws_destroy(client);
                            }
                        }
                    }
                }
            }
        }
    }

    g_gateway.running = true;
    log_info("Gateway started");
    return true;
}

void gateway_stop(void) {
    if (!g_gateway.running) {
        log_info("Gateway is not running");
        return;
    }

    // Stop Feishu WebSocket clients
    for (int i = 0; i < g_gateway.feishu_ws_count; i++) {
        if (g_gateway.feishu_ws_clients[i]) {
            feishu_ws_stop(g_gateway.feishu_ws_clients[i]);
            feishu_ws_destroy(g_gateway.feishu_ws_clients[i]);
            g_gateway.feishu_ws_clients[i] = NULL;
        }
    }
    if (g_gateway.feishu_ws_clients) {
        free(g_gateway.feishu_ws_clients);
        g_gateway.feishu_ws_clients = NULL;
    }
    g_gateway.feishu_ws_count = 0;

    // Stop HTTP API server
    if (g_gateway.http_api_server) {
        http_api_stop(g_gateway.http_api_server);
    }

    // Stop WebSocket server only if initialized
    if (g_gateway.ws_initialized) {
        websocket_server_stop(&g_gateway.ws_server);
    }

    g_gateway.running = false;
    log_info("Gateway stopped");
}

bool gateway_is_running(void) {
    return g_gateway.running;
}

void gateway_cleanup(void) {
    if (g_gateway.running) {
        gateway_stop();
    }

    // Cleanup HTTP API server
    if (g_gateway.http_api_server) {
        http_api_cleanup(g_gateway.http_api_server);
        g_gateway.http_api_server = NULL;
    }

    // Cleanup WebSocket server only if initialized
    if (g_gateway.ws_initialized) {
        websocket_server_cleanup(&g_gateway.ws_server);
    }

    log_info("Gateway cleaned up");
}

void gateway_status(void) {
    printf("Gateway:\n");
    printf("  Status: %s\n", g_gateway.running ? "running" : "stopped");
    printf("  WebSocket Port: %d\n", g_gateway.port);
    printf("  HTTP API Port: %d\n", g_gateway.http_port);
    printf("  HTTP Server: %s\n", g_gateway.http_initialized ? "enabled" : "disabled");
    printf("  WebSocket: %s\n", g_gateway.ws_initialized ? "enabled" : "disabled");
    printf("  Feishu WS: %d client(s)\n", g_gateway.feishu_ws_count);
}

char* gateway_status_string(void) {
    size_t size = 512;
    char* buf = (char*)malloc(size);
    if (!buf) return NULL;

    snprintf(buf, size,
        "Gateway:\n"
        "  状态: %s\n"
        "  WebSocket 端口: %d\n"
        "  HTTP API 端口: %d\n"
        "  HTTP Server: %s\n"
        "  WebSocket: %s\n"
        "  Feishu WS: %d 个客户端",
        g_gateway.running ? "运行中" : "已停止",
        g_gateway.port,
        g_gateway.http_port,
        g_gateway.http_initialized ? "启用" : "禁用",
        g_gateway.ws_initialized ? "启用" : "禁用",
        g_gateway.feishu_ws_count);

    return buf;
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
