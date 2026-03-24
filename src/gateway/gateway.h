#ifndef GATEWAY_H
#define GATEWAY_H

#include <stdbool.h>
#include "websocket.h"

// Gateway structure
typedef struct {
    int port;
    bool running;
    bool ws_initialized;
    WebSocketServer ws_server;
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
bool gateway_broadcast_to_webchat(const char *message);

#endif // GATEWAY_H
