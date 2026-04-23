#ifndef AGENT_H
#define AGENT_H

#include <stdbool.h>
#include "session/message.h"
#include "session/session.h"
#include "common/queue.h"
#include "tool/tool.h"
#include "memory/memory.h"

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
bool agent_send_message_with_attachments(const char *message, Attachment** attachments, int attachment_count);
bool agent_register_tool(const char *name, const char *description, const char *parameters_schema, int (*execute)(ToolArgs* args, char** result, int* result_len));
int agent_execute_tool(const char *name, ToolArgs* args, char** result, int* result_len);
void agent_list_tools(void);
void agent_set_debug_mode(bool enabled);
const char* agent_get_model(void);
bool agent_set_model(const char* model);
bool agent_memory_set(const char *key, const char *value);
char *agent_memory_get(const char *key);
bool agent_memory_clear(void);
bool agent_memory_delete(const char *key);
char *agent_get_history(int limit, int page, const char *query);

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

// Worker thread functions
bool agent_start_worker_thread(void);
void agent_stop_worker_thread(void);

#endif // AGENT_H
