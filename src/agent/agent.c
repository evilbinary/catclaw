#include "agent.h"
#include "context.h"
#include "common/config.h"
#include "gateway/channels.h"
#include "model/ai_model.h"
#include "tool/skill.h"
#include "common/workspace.h"
#include "session/session.h"
#include "session/message.h"
#include "tool/tool.h"
#include "memory/memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdarg.h>
#include "common/log.h"

// Global agent instance
Agent g_agent = {
    .model = NULL,
    .running = false,
    .debug_mode = false,
    .status = AGENT_STATUS_IDLE,
    .error_message = NULL,
    .session_manager = NULL,
    .message_queue = NULL,
    .tool_registry = NULL,
    .memory_manager = NULL,
    .steps = NULL,
    .step_count = 0,
    .step_capacity = 10,
    .current_step = 0
};

// Debug logging helper
static void debug_log(const char *format, ...) {
    if (g_agent.debug_mode) {
        va_list args;
        va_start(args, format);
        printf("[DEBUG] ");
        vprintf(format, args);
        printf("\n");
        va_end(args);
    }
}

// Memory system functions
bool agent_memory_set(const char *key, const char *value) {
    debug_log("Memory set: %s = %s", key, value);
    return memory_set(g_agent.memory_manager, key, value);
}

char *agent_memory_get(const char *key) {
    char *value = memory_get(g_agent.memory_manager, key);
    debug_log("Memory get: %s = %s", key, value);
    return value;
}

bool agent_memory_clear(void) {
    debug_log("Memory clear: clearing all entries");
    return memory_clear(g_agent.memory_manager);
}

bool agent_memory_delete(const char *key) {
    debug_log("Memory delete: %s", key);
    return memory_delete(g_agent.memory_manager, key);
}

void agent_set_debug_mode(bool enabled) {
    g_agent.debug_mode = enabled;
    printf("Debug mode %s\n", enabled ? "enabled" : "disabled");
}

const char* agent_get_model(void) {
    return g_agent.model;
}

bool agent_set_model(const char* model) {
    if (!model) return false;
    
    if (g_agent.model) {
        free(g_agent.model);
    }
    g_agent.model = strdup(model);
    return g_agent.model != NULL;
}






// Tool registration
bool agent_register_tool(const char *name, const char *description, const char *parameters_schema, int (*execute)(ToolArgs* args, char** result, int* result_len)) {
    debug_log("Registering tool: %s", name);
    
    // Create tool
    Tool* tool = tool_create(name, description, parameters_schema, execute);
    if (!tool) {
        return false;
    }
    
    // Register tool
    return tool_registry_register(g_agent.tool_registry, tool);
}

// Execute a tool
int agent_execute_tool(const char *name, ToolArgs* args, char** result, int* result_len) {
    debug_log("Executing tool: %s", name);
    
    // Get tool
    Tool* tool = tool_registry_get(g_agent.tool_registry, name);
    if (!tool) {
        *result = strdup("Error: Tool not found");
        *result_len = strlen(*result);
        return -1;
    }
    
    // Execute tool
    return tool->execute(args, result, result_len);
}

// List all tools
void agent_list_tools(void) {
    tool_registry_list(g_agent.tool_registry);
}

bool agent_init(void) {
    // Initialize workspace
    log_debug("Initializing workspace with path: %s\n", 
              g_config.workspace_path ? g_config.workspace_path : "(null)");
    
    // Use workspace.path if workspace_path is not set
    const char* workspace_path = g_config.workspace_path ? g_config.workspace_path : g_config.workspace.path;
    if (!workspace_path) {
        log_error("Workspace path is not set\n");
        return false;
    }
    
    if (!workspace_init(workspace_path)) {
        log_error("Failed to initialize workspace at: %s\n", workspace_path);
        return false;
    }

    // Create model string from provider and name
    char model[256];
    const char* model_provider = g_config.model_provider ? g_config.model_provider : "anthropic";
    const char* model_name = g_config.model_name ? g_config.model_name : "claude-3-opus-20240229";
    snprintf(model, sizeof(model), "%s/%s", model_provider, model_name);
    g_agent.model = strdup(model);
    if (!g_agent.model) {
        perror("strdup");
        workspace_cleanup();
        return false;
    }

    // Initialize AI model
    AIModelConfig model_config = {0};
    
    // Use new config structure with fallback to legacy fields
    const char* provider = g_config.model.provider ? g_config.model.provider : 
                          (g_config.model_provider ? g_config.model_provider : "llama");
    printf("[DEBUG] ai_model_init: provider=%s\n", provider);
    
    // Use new config structure with fallback to legacy fields
    const char* ai_model_name = g_config.model.model_name ? g_config.model.model_name :
                               (g_config.model.name ? g_config.model.name :
                               (g_config.model_name ? g_config.model_name : "llama3.2"));
    const char* base_url = g_config.model.base_url ? g_config.model.base_url : 
                          (g_config.api_base_url ? g_config.api_base_url : "http://localhost:11434/api/generate");
    const char* api_key = g_config.model.api_key ? g_config.model.api_key : 
                         (g_config.api_key ? g_config.api_key : NULL);
    
    printf("[DEBUG] ai_model_init: model_name=%s, base_url=%s\n", ai_model_name, base_url);
    
    model_config.provider = (char*)provider;
    model_config.model_name = (char*)ai_model_name;
    model_config.api_key = (char*)(api_key ? api_key : (getenv("ANTHROPIC_API_KEY") ? getenv("ANTHROPIC_API_KEY") : getenv("OPENAI_API_KEY")));
    model_config.base_url = (char*)base_url;
    model_config.temperature = g_config.model.temperature > 0 ? g_config.model.temperature : 0.7f;
    model_config.max_tokens = g_config.model.max_tokens > 0 ? g_config.model.max_tokens : 1024;
    model_config.stream = g_config.model.stream;
    model_config.reasoning_effort = g_config.model.reasoning_effort;

    if (!ai_model_init(&model_config)) {
        log_error("Failed to initialize AI model\n");
        free(g_agent.model);
        g_agent.model = NULL;
        workspace_cleanup();
        return false;
    }

    // Initialize session manager
    char sessions_dir[512];
    // Reuse workspace_path from earlier in the function
    if (!workspace_path) {
        log_error("Workspace path is not configured\n");
        free(g_agent.model);
        g_agent.model = NULL;
        ai_model_cleanup();
        return false;
    }
    snprintf(sessions_dir, sizeof(sessions_dir), "%s/sessions", workspace_path);
    log_info("Session manager sessions_dir: %s", sessions_dir);
    printf("[DEBUG] Session manager sessions_dir: %s\n", sessions_dir);
    g_agent.session_manager = session_manager_init(sessions_dir, g_config.session.max_sessions);
    if (!g_agent.session_manager) {
        log_error("Failed to initialize session manager\n");
        free(g_agent.model);
        g_agent.model = NULL;
        ai_model_cleanup();
        return false;
    }

    // Initialize message queue
    g_agent.message_queue = queue_init(100);
    if (!g_agent.message_queue) {
        log_error("Failed to initialize message queue\n");
        session_manager_destroy(g_agent.session_manager);
        free(g_agent.model);
        g_agent.model = NULL;
        ai_model_cleanup();
        return false;
    }

    // Initialize tool registry
    g_agent.tool_registry = tool_registry_init();
    if (!g_agent.tool_registry) {
        log_error("Failed to initialize tool registry\n");
        queue_destroy(g_agent.message_queue);
        session_manager_destroy(g_agent.session_manager);
        free(g_agent.model);
        g_agent.model = NULL;
        ai_model_cleanup();
        return false;
    }

    // Initialize memory manager
    g_agent.memory_manager = memory_manager_init(g_config.workspace_path);
    if (!g_agent.memory_manager) {
        log_error("Failed to initialize memory manager\n");
        tool_registry_destroy(g_agent.tool_registry);
        queue_destroy(g_agent.message_queue);
        session_manager_destroy(g_agent.session_manager);
        free(g_agent.model);
        g_agent.model = NULL;
        ai_model_cleanup();
        return false;
    }

    // Initialize steps array
    g_agent.steps = (Step *)malloc(sizeof(Step) * g_agent.step_capacity);
    if (!g_agent.steps) {
        log_error("Failed to allocate steps array\n");
        memory_manager_destroy(g_agent.memory_manager);
        tool_registry_destroy(g_agent.tool_registry);
        queue_destroy(g_agent.message_queue);
        session_manager_destroy(g_agent.session_manager);
        free(g_agent.model);
        g_agent.model = NULL;
        ai_model_cleanup();
        return false;
    }

    // Register default tools
    agent_register_tool("calculator", "Perform basic arithmetic calculations", NULL, tool_calculate);
    agent_register_tool("time", "Get current time", NULL, tool_get_time);
    agent_register_tool("reverse-string", "Reverse a string", NULL, tool_reverse_string);
    agent_register_tool("read-file", "Read a file from disk", NULL, tool_read_file);
    agent_register_tool("write-file", "Write content to a file", NULL, tool_write_file);
    agent_register_tool("web-search", "Simulate web search", NULL, tool_search_web);
    agent_register_tool("memory-save", "Save a key-value pair to memory", NULL, tool_save_memory);
    agent_register_tool("memory-load", "Load a value from memory by key (no key = list all)", NULL, tool_read_memory);
    agent_register_tool("memory-delete", "Delete a key-value pair from memory", NULL, tool_delete_memory);
    agent_register_tool("get-weather", "Get weather information for a location", NULL, tool_get_weather);
    agent_register_tool("list-directory", "List files and directories in a given path", NULL, tool_list_directory);
    agent_register_tool("web-fetch", "Fetch content from a URL", NULL, tool_web_fetch);
    agent_register_tool("shell", "Execute shell commands", NULL, tool_shell_execute);
    
    // Skill discovery tools
    agent_register_tool("skill-search", "Search local skills by query keyword", NULL, tool_skill_search);
    agent_register_tool("skill-match", "Discover and match relevant skills by keyword (for agent auto-discovery)", NULL, tool_skill_match);
    agent_register_tool("skill-preview", "Preview skill content without full loading", NULL, tool_skill_preview);

    g_agent.running = true;
    
    // Load default session from disk if exists
    Session* default_session = session_manager_get_or_create(g_agent.session_manager, "default");
    if (default_session) {
        log_info("Default session loaded with %d messages in history", default_session->history->count);
    } else {
        log_info("No existing default session found, creating new session");
    }
    
    // Note: Worker thread will be started by main.c after agent initialization
    
    printf("Agent initialized with model: %s\n", g_agent.model);
    agent_list_tools();
    return true;
}

void agent_cleanup(void) {
    // Stop worker thread
    agent_stop_worker_thread();

    if (g_agent.model) {
        free(g_agent.model);
        g_agent.model = NULL;
    }

    // Cleanup session manager
    if (g_agent.session_manager) {
        session_manager_destroy(g_agent.session_manager);
        g_agent.session_manager = NULL;
    }

    // Cleanup message queue
    if (g_agent.message_queue) {
        queue_destroy(g_agent.message_queue);
        g_agent.message_queue = NULL;
    }

    // Cleanup tool registry
    if (g_agent.tool_registry) {
        tool_registry_destroy(g_agent.tool_registry);
        g_agent.tool_registry = NULL;
    }

    // Cleanup memory manager
    if (g_agent.memory_manager) {
        memory_manager_destroy(g_agent.memory_manager);
        g_agent.memory_manager = NULL;
    }

    // Cleanup steps
    agent_clear_steps();
    if (g_agent.steps) {
        free(g_agent.steps);
        g_agent.steps = NULL;
        g_agent.step_count = 0;
        g_agent.step_capacity = 10;
        g_agent.current_step = 0;
    }

    // Cleanup error message
    agent_clear_error();

    // Cleanup AI model
    ai_model_cleanup();

    // Cleanup workspace
    workspace_cleanup();

    g_agent.running = false;
    printf("Agent cleaned up\n");
}

bool agent_send_message(const char *message) {
    if (!g_agent.running) {
        log_error("Agent is not running\n");
        return false;
    }

    printf("Agent received message: %s\n", message);

    // Create a new message
    Message *msg = message_create(ROLE_USER, message);
    if (!msg) {
        log_error("Failed to create message\n");
        return false;
    }

    // Create a queue item
    QueueItem *item = queue_item_create("default", msg, QUEUE_MODE_COLLECT);
    if (!item) {
        message_destroy(msg);
        log_error("Failed to create queue item\n");
        return false;
    }

    // Enqueue the message
    if (!queue_enqueue(g_agent.message_queue, item)) {
        queue_item_destroy(item);
        log_error("Failed to enqueue message\n");
        return false;
    }

    printf("Message enqueued for processing\n");
    return true;
}

// Error handling functions
void agent_set_error(const char *format, ...) {
    va_list args;
    va_start(args, format);
    
    // Free existing error message
    if (g_agent.error_message) {
        free(g_agent.error_message);
        g_agent.error_message = NULL;
    }
    
    // Allocate buffer for error message
    char buffer[1024];
    vsnprintf(buffer, 1024, format, args);
    
    g_agent.error_message = strdup(buffer);
    g_agent.status = AGENT_STATUS_ERROR;
    
    debug_log("Error: %s", buffer);
    
    va_end(args);
}

void agent_clear_error(void) {
    if (g_agent.error_message) {
        free(g_agent.error_message);
        g_agent.error_message = NULL;
    }
    g_agent.status = AGENT_STATUS_IDLE;
}

const char *agent_get_error(void) {
    return g_agent.error_message;
}

// Multi-step execution functions
bool agent_add_step(const char *description, const char *tool_name, const char *params) {
    debug_log("Adding step: %s", description);
    
    // Resize steps array if needed
    if (g_agent.step_count >= g_agent.step_capacity) {
        g_agent.step_capacity *= 2;
        Step *new_steps = (Step *)realloc(g_agent.steps, sizeof(Step) * g_agent.step_capacity);
        if (!new_steps) {
            return false;
        }
        g_agent.steps = new_steps;
    }
    
    // Create step ID
    char id[32];
    snprintf(id, 32, "step_%d", g_agent.step_count);
    
    // Add step
    g_agent.steps[g_agent.step_count].id = strdup(id);
    g_agent.steps[g_agent.step_count].description = strdup(description);
    g_agent.steps[g_agent.step_count].tool_name = strdup(tool_name);
    g_agent.steps[g_agent.step_count].params = strdup(params);
    g_agent.steps[g_agent.step_count].result = NULL;
    g_agent.steps[g_agent.step_count].completed = false;
    g_agent.step_count++;
    
    return true;
}

bool agent_execute_steps(void) {
    if (g_agent.step_count == 0) {
        printf("No steps to execute\n");
        return false;
    }
    
    g_agent.status = AGENT_STATUS_EXECUTING;
    g_agent.current_step = 0;
    
    printf("Starting execution of %d steps\n", g_agent.step_count);
    
    while (g_agent.current_step < g_agent.step_count && g_agent.status == AGENT_STATUS_EXECUTING) {
        Step *step = &g_agent.steps[g_agent.current_step];
        printf("Executing step %d: %s\n", g_agent.current_step + 1, step->description);
        
        // Execute the tool
        char *tool_result;
        int result_len;
        ToolArgs* args = tool_args_from_string(step->params);
        if (agent_execute_tool(step->tool_name, args, &tool_result, &result_len) == 0) {
            step->result = strdup(tool_result);
            step->completed = true;
            printf("Step %d result: %s\n", g_agent.current_step + 1, tool_result);
            free(tool_result);
        } else {
            step->result = strdup("Error executing tool");
            step->completed = true;
            printf("Step %d result: Error executing tool\n", g_agent.current_step + 1);
        }
        tool_args_free(args);
        
        g_agent.current_step++;
    }
    
    if (g_agent.status == AGENT_STATUS_EXECUTING) {
        g_agent.status = AGENT_STATUS_IDLE;
        printf("All steps completed\n");
    }
    
    return true;
}

bool agent_pause_execution(void) {
    if (g_agent.status != AGENT_STATUS_EXECUTING) {
        printf("Agent is not executing\n");
        return false;
    }
    
    g_agent.status = AGENT_STATUS_PAUSED;
    printf("Execution paused at step %d\n", g_agent.current_step + 1);
    return true;
}

bool agent_resume_execution(void) {
    if (g_agent.status != AGENT_STATUS_PAUSED) {
        printf("Agent is not paused\n");
        return false;
    }
    
    g_agent.status = AGENT_STATUS_EXECUTING;
    printf("Resuming execution from step %d\n", g_agent.current_step + 1);
    
    while (g_agent.current_step < g_agent.step_count && g_agent.status == AGENT_STATUS_EXECUTING) {
        Step *step = &g_agent.steps[g_agent.current_step];
        printf("Executing step %d: %s\n", g_agent.current_step + 1, step->description);
        
        // Execute the tool
        char *tool_result;
        int result_len;
        ToolArgs* args = tool_args_from_string(step->params);
        if (agent_execute_tool(step->tool_name, args, &tool_result, &result_len) == 0) {
            step->result = strdup(tool_result);
            step->completed = true;
            printf("Step %d result: %s\n", g_agent.current_step + 1, tool_result);
            free(tool_result);
        } else {
            step->result = strdup("Error executing tool");
            step->completed = true;
            printf("Step %d result: Error executing tool\n", g_agent.current_step + 1);
        }
        tool_args_free(args);
        
        g_agent.current_step++;
    }
    
    if (g_agent.status == AGENT_STATUS_EXECUTING) {
        g_agent.status = AGENT_STATUS_IDLE;
        printf("All steps completed\n");
    }
    
    return true;
}

bool agent_stop_execution(void) {
    if (g_agent.status != AGENT_STATUS_EXECUTING && g_agent.status != AGENT_STATUS_PAUSED) {
        printf("Agent is not executing\n");
        return false;
    }
    
    g_agent.status = AGENT_STATUS_IDLE;
    g_agent.current_step = 0;
    printf("Execution stopped\n");
    return true;
}

void agent_clear_steps(void) {
    for (int i = 0; i < g_agent.step_count; i++) {
        free(g_agent.steps[i].id);
        free(g_agent.steps[i].description);
        free(g_agent.steps[i].tool_name);
        free(g_agent.steps[i].params);
        if (g_agent.steps[i].result) {
            free(g_agent.steps[i].result);
        }
    }
    g_agent.step_count = 0;
    g_agent.current_step = 0;
    printf("Steps cleared\n");
}

void agent_print_steps(void) {
    if (g_agent.step_count == 0) {
        printf("No steps defined\n");
        return;
    }
    
    printf("Defined steps:\n");
    for (int i = 0; i < g_agent.step_count; i++) {
        Step *step = &g_agent.steps[i];
        printf("  Step %d: %s\n", i + 1, step->description);
        printf("    Tool: %s\n", step->tool_name);
        printf("    Params: %s\n", step->params);
        printf("    Status: %s\n", step->completed ? "Completed" : "Pending");
        if (step->result) {
            printf("    Result: %s\n", step->result);
        }
    }
}

// Update agent_status to include multi-step execution status
void agent_status(void) {
    printf("Agent:\n");
    printf("  Model: %s\n", g_agent.model);
    
    // Determine provider based on model
    const char *provider = "unknown";
    if (strstr(g_agent.model, "anthropic") != NULL) {
        provider = "anthropic";
    } else if (strstr(g_agent.model, "openai") != NULL) {
        provider = "openai";
    } else if (strstr(g_agent.model, "gemini") != NULL) {
        provider = "gemini";
    } else if (strstr(g_agent.model, "llama") != NULL) {
        provider = "llama";
    }
    printf("  Provider: %s\n", provider);
    
    // Show workspace
    printf("  Workspace: ~/.catclaw/workspace\n");
}

// Skill functions
bool agent_load_skill(const char *path) {
    return skill_load(path);
}

bool agent_unload_skill(const char *name) {
    return skill_unload(name);
}

char *agent_execute_skill(const char *name, const char *params) {
    return skill_execute_skill(name, params);
}

void agent_list_skills(void) {
    skill_list();
}

bool agent_enable_skill(const char *name) {
    return skill_enable(name);
}

bool agent_disable_skill(const char *name) {
    return skill_disable(name);
}

// Start worker thread - uses context.c implementation
bool agent_start_worker_thread(void) {
    // Create default agent node if not exists
    if (!g_default_agent_node) {
        g_default_agent_node = agent_node_create("default", g_agent.model);
        if (!g_default_agent_node) {
            log_error("Failed to create default agent node\n");
            return false;
        }
        // Copy agent components to the node
        g_default_agent_node->agent.session_manager = g_agent.session_manager;
        g_default_agent_node->agent.message_queue = g_agent.message_queue;
        g_default_agent_node->agent.tool_registry = g_agent.tool_registry;
        g_default_agent_node->agent.memory_manager = g_agent.memory_manager;
    }
    
    return agent_node_start_worker(g_default_agent_node);
}

// Stop worker thread
void agent_stop_worker_thread(void) {
    if (g_default_agent_node) {
        agent_node_stop_worker(g_default_agent_node);
    }
}
