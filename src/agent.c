#include "agent.h"
#include "config.h"
#include "channels.h"
#include "ai_model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdarg.h>

// Global agent instance
Agent g_agent = {
    .model = NULL,
    .running = false,
    .debug_mode = false,
    .tools = NULL,
    .tool_count = 0,
    .tool_capacity = 20,
    .memory = NULL,
    .memory_count = 0,
    .memory_capacity = 50
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
    
    // Check if key already exists
    for (int i = 0; i < g_agent.memory_count; i++) {
        if (strcmp(g_agent.memory[i].key, key) == 0) {
            free(g_agent.memory[i].value);
            g_agent.memory[i].value = strdup(value);
            return true;
        }
    }
    
    // Resize memory array if needed
    if (g_agent.memory_count >= g_agent.memory_capacity) {
        g_agent.memory_capacity *= 2;
        MemoryEntry *new_memory = (MemoryEntry *)realloc(g_agent.memory, sizeof(MemoryEntry) * g_agent.memory_capacity);
        if (!new_memory) {
            return false;
        }
        g_agent.memory = new_memory;
    }
    
    // Add new entry
    g_agent.memory[g_agent.memory_count].key = strdup(key);
    g_agent.memory[g_agent.memory_count].value = strdup(value);
    g_agent.memory_count++;
    
    return true;
}

char *agent_memory_get(const char *key) {
    for (int i = 0; i < g_agent.memory_count; i++) {
        if (strcmp(g_agent.memory[i].key, key) == 0) {
            debug_log("Memory get: %s = %s", key, g_agent.memory[i].value);
            return g_agent.memory[i].value;
        }
    }
    debug_log("Memory get: %s not found", key);
    return NULL;
}

bool agent_memory_clear(void) {
    debug_log("Memory clear: clearing all entries");
    for (int i = 0; i < g_agent.memory_count; i++) {
        free(g_agent.memory[i].key);
        free(g_agent.memory[i].value);
    }
    g_agent.memory_count = 0;
    return true;
}

void agent_set_debug_mode(bool enabled) {
    g_agent.debug_mode = enabled;
    printf("Debug mode %s\n", enabled ? "enabled" : "disabled");
}

// Calculator tool
static char *tool_calculator(const char *params) {
    debug_log("Calculator tool called with params: %s", params);
    char *result = (char *)malloc(256);
    if (!result) {
        return "Error: Memory allocation failed";
    }
    
    double a, b;
    char op;
    if (sscanf(params, "%lf %c %lf", &a, &op, &b) != 3) {
        snprintf(result, 256, "Error: Invalid expression format. Use 'number operator number' (e.g., '2 + 3')");
        return result;
    }
    
    double res;
    switch (op) {
        case '+':
            res = a + b;
            break;
        case '-':
            res = a - b;
            break;
        case '*':
            res = a * b;
            break;
        case '/':
            if (b == 0) {
                snprintf(result, 256, "Error: Division by zero");
                return result;
            }
            res = a / b;
            break;
        default:
            snprintf(result, 256, "Error: Unsupported operator. Use +, -, *, or /");
            return result;
    }
    
    snprintf(result, 256, "Result: %f", res);
    return result;
}

// Time tool
static char *tool_time(const char *params) {
    debug_log("Time tool called with params: %s", params);
    char *result = (char *)malloc(256);
    if (!result) {
        return "Error: Memory allocation failed";
    }
    
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[100];
    strftime(time_str, 100, "%Y-%m-%d %H:%M:%S", tm_info);
    
    snprintf(result, 256, "Current time: %s", time_str);
    return result;
}

// String reverse tool
static char *tool_reverse_string(const char *params) {
    debug_log("Reverse string tool called with params: %s", params);
    char *result = (char *)malloc(strlen(params) + 2);
    if (!result) {
        return "Error: Memory allocation failed";
    }
    
    int len = strlen(params);
    for (int i = 0; i < len; i++) {
        result[i] = params[len - i - 1];
    }
    result[len] = '\0';
    
    return result;
}

// File read tool
static char *tool_read_file(const char *params) {
    debug_log("Read file tool called with params: %s", params);
    char *result = (char *)malloc(4096);
    if (!result) {
        return "Error: Memory allocation failed";
    }
    
    FILE *file = fopen(params, "r");
    if (!file) {
        snprintf(result, 4096, "Error: Could not open file %s", params);
        return result;
    }
    
    size_t read_bytes = fread(result, 1, 4095, file);
    result[read_bytes] = '\0';
    fclose(file);
    
    return result;
}

// File write tool
static char *tool_write_file(const char *params) {
    debug_log("Write file tool called with params: %s", params);
    char *result = (char *)malloc(256);
    if (!result) {
        return "Error: Memory allocation failed";
    }
    
    // Split params into filename and content
    char *space_pos = strchr(params, ' ');
    if (!space_pos) {
        snprintf(result, 256, "Error: Invalid format. Use 'filename content'");
        return result;
    }
    
    char *filename = (char *)malloc(space_pos - params + 1);
    strncpy(filename, params, space_pos - params);
    filename[space_pos - params] = '\0';
    char *content = space_pos + 1;
    
    FILE *file = fopen(filename, "w");
    if (!file) {
        snprintf(result, 256, "Error: Could not open file %s for writing", filename);
        free(filename);
        return result;
    }
    
    fprintf(file, "%s", content);
    fclose(file);
    free(filename);
    
    snprintf(result, 256, "Successfully wrote to file %s", params);
    return result;
}

// Simulated web search tool
static char *tool_web_search(const char *params) {
    debug_log("Web search tool called with params: %s", params);
    char *result = (char *)malloc(512);
    if (!result) {
        return "Error: Memory allocation failed";
    }
    
    // Simulate search results
    snprintf(result, 512, 
             "Search results for '%s':\n"
             "1. Wikipedia article about %s\n"
             "2. StackOverflow questions related to %s\n"
             "3. Documentation for %s", 
             params, params, params, params);
    return result;
}

// Memory save tool
static char *tool_memory_save(const char *params) {
    debug_log("Memory save tool called with params: %s", params);
    char *result = (char *)malloc(256);
    if (!result) {
        return "Error: Memory allocation failed";
    }
    
    // Split params into key and value
    char *space_pos = strchr(params, ' ');
    if (!space_pos) {
        snprintf(result, 256, "Error: Invalid format. Use 'key value'");
        return result;
    }
    
    char *key = (char *)malloc(space_pos - params + 1);
    strncpy(key, params, space_pos - params);
    key[space_pos - params] = '\0';
    char *value = space_pos + 1;
    
    if (agent_memory_set(key, value)) {
        snprintf(result, 256, "Saved to memory: %s = %s", key, value);
    } else {
        snprintf(result, 256, "Error: Failed to save to memory");
    }
    
    free(key);
    return result;
}

// Memory load tool
static char *tool_memory_load(const char *params) {
    debug_log("Memory load tool called with params: %s", params);
    char *result = (char *)malloc(256);
    if (!result) {
        return "Error: Memory allocation failed";
    }
    
    char *value = agent_memory_get(params);
    if (value) {
        snprintf(result, 256, "Memory value: %s = %s", params, value);
    } else {
        snprintf(result, 256, "Error: Key '%s' not found in memory", params);
    }
    
    return result;
}

// Tool registration
bool agent_register_tool(const char *name, const char *description, char *(*execute)(const char *params)) {
    debug_log("Registering tool: %s", name);
    
    // Check if tool already exists
    for (int i = 0; i < g_agent.tool_count; i++) {
        if (strcmp(g_agent.tools[i].name, name) == 0) {
            return false;
        }
    }
    
    // Resize tool array if needed
    if (g_agent.tool_count >= g_agent.tool_capacity) {
        g_agent.tool_capacity *= 2;
        Tool *new_tools = (Tool *)realloc(g_agent.tools, sizeof(Tool) * g_agent.tool_capacity);
        if (!new_tools) {
            return false;
        }
        g_agent.tools = new_tools;
    }
    
    // Add tool
    g_agent.tools[g_agent.tool_count].name = strdup(name);
    g_agent.tools[g_agent.tool_count].description = strdup(description);
    g_agent.tools[g_agent.tool_count].execute = execute;
    g_agent.tool_count++;
    
    return true;
}

// Execute a tool
char *agent_execute_tool(const char *name, const char *params) {
    debug_log("Executing tool: %s with params: %s", name, params);
    for (int i = 0; i < g_agent.tool_count; i++) {
        if (strcmp(g_agent.tools[i].name, name) == 0) {
            return g_agent.tools[i].execute(params);
        }
    }
    char *error = (char *)malloc(256);
    snprintf(error, 256, "Error: Tool '%s' not found", name);
    return error;
}

// List all tools
void agent_list_tools(void) {
    if (g_agent.tool_count == 0) {
        printf("No tools registered\n");
        return;
    }
    
    printf("Available tools:\n");
    for (int i = 0; i < g_agent.tool_count; i++) {
        printf("  %s: %s\n", g_agent.tools[i].name, g_agent.tools[i].description);
    }
}

// Parse user command and execute tools
static char *agent_parse_command(const char *command) {
    char *result = (char *)malloc(2048);
    if (!result) {
        return "Error: Memory allocation failed";
    }
    
    result[0] = '\0';
    debug_log("Parsing command: %s", command);
    
    // Check for debug commands
    if (strstr(command, "debug on") != NULL) {
        agent_set_debug_mode(true);
        snprintf(result, 2048, "Debug mode enabled");
        return result;
    } else if (strstr(command, "debug off") != NULL) {
        agent_set_debug_mode(false);
        snprintf(result, 2048, "Debug mode disabled");
        return result;
    }
    
    // Simple command parsing
    if (strstr(command, "calculate") != NULL || strstr(command, "calc") != NULL) {
        // Extract expression
        char *expr = strchr(command, ' ');
        if (expr) {
            expr++;
            char *tool_result = agent_execute_tool("calculator", expr);
            snprintf(result, 2048, "Calculator result: %s", tool_result);
            free(tool_result);
        } else {
            snprintf(result, 2048, "Error: No expression provided for calculator");
        }
    } else if (strstr(command, "time") != NULL || strstr(command, "clock") != NULL || strstr(command, "now") != NULL) {
        char *tool_result = agent_execute_tool("time", "");
        snprintf(result, 2048, "%s", tool_result);
        free(tool_result);
    } else if (strstr(command, "reverse") != NULL && strstr(command, "string") != NULL) {
        // Extract string to reverse
        char *str = strstr(command, "string");
        if (str) {
            str += 7; // Skip "string "
            char *tool_result = agent_execute_tool("reverse_string", str);
            snprintf(result, 2048, "Reversed string: %s", tool_result);
            free(tool_result);
        } else {
            snprintf(result, 2048, "Error: No string provided for reversal");
        }
    } else if (strstr(command, "read file") != NULL) {
        char *str = strstr(command, "read file");
        if (str) {
            str += 10; // Skip "read file "
            char *tool_result = agent_execute_tool("read_file", str);
            snprintf(result, 2048, "File content:\n%s", tool_result);
            free(tool_result);
        } else {
            snprintf(result, 2048, "Error: No filename provided");
        }
    } else if (strstr(command, "write file") != NULL) {
        char *str = strstr(command, "write file");
        if (str) {
            str += 11; // Skip "write file "
            char *tool_result = agent_execute_tool("write_file", str);
            snprintf(result, 2048, "%s", tool_result);
            free(tool_result);
        } else {
            snprintf(result, 2048, "Error: Invalid format. Use 'write file filename content'");
        }
    } else if (strstr(command, "search") != NULL || strstr(command, "web search") != NULL) {
        char *str = strstr(command, "search");
        if (!str) {
            str = strstr(command, "web search");
            str += 11;
        } else {
            str += 7;
        }
        char *tool_result = agent_execute_tool("web_search", str);
        snprintf(result, 2048, "%s", tool_result);
        free(tool_result);
    } else if (strstr(command, "memory save") != NULL) {
        char *str = strstr(command, "memory save");
        if (str) {
            str += 12; // Skip "memory save "
            char *tool_result = agent_execute_tool("memory_save", str);
            snprintf(result, 2048, "%s", tool_result);
            free(tool_result);
        } else {
            snprintf(result, 2048, "Error: Invalid format. Use 'memory save key value'");
        }
    } else if (strstr(command, "memory load") != NULL) {
        char *str = strstr(command, "memory load");
        if (str) {
            str += 12; // Skip "memory load "
            char *tool_result = agent_execute_tool("memory_load", str);
            snprintf(result, 2048, "%s", tool_result);
            free(tool_result);
        } else {
            snprintf(result, 2048, "Error: No key provided");
        }
    } else if (strstr(command, "list tools") != NULL || strstr(command, "tools") != NULL) {
        agent_list_tools();
        snprintf(result, 2048, "Tools listed above");
    } else {
        // Send to AI model if no tool match
        AIModelResponse *response = ai_model_send_message(command);
        if (response) {
            if (response->success) {
                snprintf(result, 2048, "AI response: %s", response->content);
            } else {
                snprintf(result, 2048, "AI model error: %s", response->error);
            }
            ai_model_free_response(response);
        } else {
            snprintf(result, 2048, "Error: Failed to get response from AI model");
        }
    }
    
    return result;
}

bool agent_init(void) {
    g_agent.model = strdup(g_config.model);
    if (!g_agent.model) {
        perror("strdup");
        return false;
    }

    // Initialize AI model
    AIModelConfig model_config;
    if (strstr(g_config.model, "anthropic") != NULL) {
        model_config.type = AI_MODEL_ANTHROPIC;
        model_config.model_name = strstr(g_config.model, "/") ? strstr(g_config.model, "/") + 1 : g_config.model;
    } else if (strstr(g_config.model, "openai") != NULL) {
        model_config.type = AI_MODEL_OPENAI;
        model_config.model_name = strstr(g_config.model, "/") ? strstr(g_config.model, "/") + 1 : g_config.model;
    } else {
        model_config.type = AI_MODEL_ANTHROPIC;
        model_config.model_name = "claude-3-opus-20240229";
    }
    model_config.api_key = getenv("ANTHROPIC_API_KEY") ? getenv("ANTHROPIC_API_KEY") : getenv("OPENAI_API_KEY");
    model_config.base_url = NULL;

    if (!ai_model_init(&model_config)) {
        fprintf(stderr, "Failed to initialize AI model\n");
        free(g_agent.model);
        g_agent.model = NULL;
        return false;
    }

    // Initialize tool array
    g_agent.tools = (Tool *)malloc(sizeof(Tool) * g_agent.tool_capacity);
    if (!g_agent.tools) {
        fprintf(stderr, "Failed to allocate tools array\n");
        free(g_agent.model);
        g_agent.model = NULL;
        ai_model_cleanup();
        return false;
    }

    // Initialize memory array
    g_agent.memory = (MemoryEntry *)malloc(sizeof(MemoryEntry) * g_agent.memory_capacity);
    if (!g_agent.memory) {
        fprintf(stderr, "Failed to allocate memory array\n");
        free(g_agent.tools);
        free(g_agent.model);
        g_agent.model = NULL;
        ai_model_cleanup();
        return false;
    }

    // Register default tools
    agent_register_tool("calculator", "Perform basic arithmetic calculations", tool_calculator);
    agent_register_tool("time", "Get current time", tool_time);
    agent_register_tool("reverse_string", "Reverse a string", tool_reverse_string);
    agent_register_tool("read_file", "Read a file from disk", tool_read_file);
    agent_register_tool("write_file", "Write content to a file", tool_write_file);
    agent_register_tool("web_search", "Simulate web search", tool_web_search);
    agent_register_tool("memory_save", "Save a key-value pair to memory", tool_memory_save);
    agent_register_tool("memory_load", "Load a value from memory by key", tool_memory_load);

    g_agent.running = true;
    printf("Agent initialized with model: %s\n", g_agent.model);
    agent_list_tools();
    return true;
}

void agent_cleanup(void) {
    if (g_agent.model) {
        free(g_agent.model);
        g_agent.model = NULL;
    }

    // Cleanup tools
    if (g_agent.tools) {
        for (int i = 0; i < g_agent.tool_count; i++) {
            free(g_agent.tools[i].name);
            free(g_agent.tools[i].description);
        }
        free(g_agent.tools);
        g_agent.tools = NULL;
        g_agent.tool_count = 0;
        g_agent.tool_capacity = 20;
    }

    // Cleanup memory
    agent_memory_clear();
    if (g_agent.memory) {
        free(g_agent.memory);
        g_agent.memory = NULL;
        g_agent.memory_count = 0;
        g_agent.memory_capacity = 50;
    }

    // Cleanup AI model
    ai_model_cleanup();

    g_agent.running = false;
    printf("Agent cleaned up\n");
}

void agent_status(void) {
    printf("  Agent: %s\n", g_agent.running ? "running" : "stopped");
    printf("  Model: %s\n", g_agent.model);
    printf("  Tools: %d\n", g_agent.tool_count);
    printf("  Memory entries: %d\n", g_agent.memory_count);
    printf("  Debug mode: %s\n", g_agent.debug_mode ? "enabled" : "disabled");
}

bool agent_send_message(const char *message) {
    if (!g_agent.running) {
        fprintf(stderr, "Agent is not running\n");
        return false;
    }

    printf("Agent received message: %s\n", message);

    // Parse command and execute
    char *result = agent_parse_command(message);
    printf("Agent response: %s\n", result);
    
    // Send response to WebChat channel
    channel_send_message(CHANNEL_WEBCHAT, result);
    
    free(result);
    return true;
}
