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

// Step structure for multi-step execution
typedef struct {
    char *id;
    char *description;
    char *tool_name;
    char *params;
    char *result;
    bool completed;
} Step;

// Agent status
typedef enum {
    AGENT_STATUS_IDLE,
    AGENT_STATUS_EXECUTING,
    AGENT_STATUS_PAUSED,
    AGENT_STATUS_ERROR
} AgentStatus;

// Agent structure
typedef struct {
    char *model;
    bool running;
    bool debug_mode;
    AgentStatus status;
    char *error_message;
    Tool *tools;
    int tool_count;
    int tool_capacity;
    MemoryEntry *memory;
    int memory_count;
    int memory_capacity;
    Step *steps;
    int step_count;
    int step_capacity;
    int current_step;
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

// Multi-step execution functions
bool agent_add_step(const char *description, const char *tool_name, const char *params);
bool agent_execute_steps(void);
bool agent_pause_execution(void);
bool agent_resume_execution(void);
bool agent_stop_execution(void);
void agent_clear_steps(void);
void agent_print_steps(void);

// Error handling functions
void agent_set_error(const char *format, ...);
void agent_clear_error(void);
const char *agent_get_error(void);

#endif // AGENT_H
