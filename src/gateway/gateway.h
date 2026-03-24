#ifndef GATEWAY_H
#define GATEWAY_H

#include <stdbool.h>
#include "websocket.h"
#include "http_server.h"
#include "feishu_ws.h"

// Gateway structure
typedef struct {
    int port;
    bool running;
    bool ws_initialized;

    // WebSocket server (for WebChat)
    WebSocketServer ws_server;

    // HTTP API server
    HttpServer* http_api_server;
    int http_port;
    bool http_initialized;

    // Feishu WebSocket clients (支持多个飞书频道)
    FeishuWsClient** feishu_ws_clients;
    int feishu_ws_count;
    int feishu_ws_capacity;
} Gateway;

// Global gateway instance
extern Gateway g_gateway;

// Functions
bool gateway_init(void);
bool gateway_start(void);
void gateway_stop(void);
bool gateway_is_running(void);
void gateway_cleanup(void);
void gateway_status(void);
char* gateway_status_string(void);  // 返回状态字符串(需要调用者free)
bool gateway_broadcast_to_webchat(const char *message);

#endif // GATEWAY_H
