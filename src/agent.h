#ifndef AGENT_H
#define AGENT_H

#include <stdbool.h>

// Agent structure
typedef struct {
    char *model;
    bool running;
} Agent;

// Global agent instance
extern Agent g_agent;

// Functions
bool agent_init(void);
void agent_cleanup(void);
void agent_status(void);
bool agent_send_message(const char *message);

#endif // AGENT_H
