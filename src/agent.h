#ifndef AGENT_H
#define AGENT_H

#include <stdbool.h>
#include "message.h"
#include "session.h"
#include "queue.h"
#include "tool.h"
#include "memory.h"





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
    SessionManager* session_manager;
    MessageQueue* message_queue;
    ToolRegistry* tool_registry;
    MemoryManager* memory_manager;
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
bool agent_register_tool(const char *name, const char *description, const char *parameters_schema, int (*execute)(const char *args, char** result, int* result_len));
int agent_execute_tool(const char *name, const char *args, char** result, int* result_len);
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

// Skill functions
bool agent_load_skill(const char *path);
bool agent_unload_skill(const char *name);
char *agent_execute_skill(const char *name, const char *params);
void agent_list_skills(void);
bool agent_enable_skill(const char *name);
bool agent_disable_skill(const char *name);

// Command parsing function
char *agent_parse_command(const char *command);

#endif // AGENT_H
