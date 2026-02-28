#ifndef AGENT_H
#define AGENT_H

#include <stdbool.h>

// Tool structure
typedef struct {
    char *name;
    char *description;
    char *(*execute)(const char *params);
} Tool;

// Agent structure
typedef struct {
    char *model;
    bool running;
    Tool *tools;
    int tool_count;
    int tool_capacity;
} Agent;

// Global agent instance
extern Agent g_agent;

// Functions
bool agent_init(void);
void agent_cleanup(void);
void agent_status(void);
bool agent_send_message(const char *message);
bool agent_register_tool(const char *name, const char *description, char *(*execute)(const char *params));
char *agent_execute_tool(const char *name, const char *params);
void agent_list_tools(void);

#endif // AGENT_H
