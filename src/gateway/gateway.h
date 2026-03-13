#ifndef GATEWAY_H
#define GATEWAY_H

#include <stdbool.h>
#include "websocket.h"

// Gateway structure
typedef struct {
    int port;
    bool running;
    WebSocketServer ws_server;
} Gateway;

// Global gateway instance
extern Gateway g_gateway;

// Functions
bool gateway_init(void);
bool gateway_start(void);
void gateway_stop(void);
void gateway_cleanup(void);
void gateway_status(void);

#endif // GATEWAY_H
