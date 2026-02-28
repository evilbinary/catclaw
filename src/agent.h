#ifndef AGENT_H
#define AGENT_H

#include <stdbool.h>

// Tool structure
typedef struct {
    char *name;
    char *description;
    char *(*execute)(const char *params);
} Tool;

// Memory entry structure
typedef struct {
    char *key;
    char *value;
} MemoryEntry;

// Agent structure
typedef struct {
    char *model;
    bool running;
    bool debug_mode;
    Tool *tools;
    int tool_count;
    int tool_capacity;
    MemoryEntry *memory;
    int memory_count;
    int memory_capacity;
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
void agent_set_debug_mode(bool enabled);
bool agent_memory_set(const char *key, const char *value);
char *agent_memory_get(const char *key);
bool agent_memory_clear(void);

#endif // AGENT_H
