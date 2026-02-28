#include "agent.h"
#include "config.h"
#include "channels.h"
#include "ai_model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

// Global agent instance
Agent g_agent = {
    .model = NULL,
    .running = false,
    .tools = NULL,
    .tool_count = 0,
    .tool_capacity = 10
};

// Calculator tool
static char *tool_calculator(const char *params) {
    // Simple calculator that evaluates basic arithmetic expressions
    // For simplicity, we'll just handle addition, subtraction, multiplication, and division
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
    // Get current time
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
    // Reverse a string
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

// Tool registration
bool agent_register_tool(const char *name, const char *description, char *(*execute)(const char *params)) {
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
    for (int i = 0; i < g_agent.tool_count; i++) {
        if (strcmp(g_agent.tools[i].name, name) == 0) {
            return g_agent.tools[i].execute(params);
        }
    }
    return "Error: Tool not found";
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
    char *result = (char *)malloc(1024);
    if (!result) {
        return "Error: Memory allocation failed";
    }
    
    // Simple command parsing
    if (strstr(command, "calculate") != NULL || strstr(command, "calc") != NULL || strstr(command, "+") != NULL || strstr(command, "-") != NULL || strstr(command, "*") != NULL || strstr(command, "/") != NULL) {
        // Extract expression
        char *expr = strchr(command, ' ');
        if (expr) {
            expr++;
            char *tool_result = agent_execute_tool("calculator", expr);
            snprintf(result, 1024, "Calculator result: %s", tool_result);
            free(tool_result);
        } else {
            snprintf(result, 1024, "Error: No expression provided for calculator");
        }
    } else if (strstr(command, "time") != NULL || strstr(command, "clock") != NULL || strstr(command, "now") != NULL) {
        char *tool_result = agent_execute_tool("time", "");
        snprintf(result, 1024, "%s", tool_result);
        free(tool_result);
    } else if (strstr(command, "reverse") != NULL && strstr(command, "string") != NULL) {
        // Extract string to reverse
        char *str = strstr(command, "string");
        if (str) {
            str += 7; // Skip "string "
            char *tool_result = agent_execute_tool("reverse_string", str);
            snprintf(result, 1024, "Reversed string: %s", tool_result);
            free(tool_result);
        } else {
            snprintf(result, 1024, "Error: No string provided for reversal");
        }
    } else if (strstr(command, "list tools") != NULL || strstr(command, "tools") != NULL) {
        agent_list_tools();
        snprintf(result, 1024, "Tools listed above");
    } else {
        // Send to AI model if no tool match
        AIModelResponse *response = ai_model_send_message(command);
        if (response) {
            if (response->success) {
                snprintf(result, 1024, "AI response: %s", response->content);
            } else {
                snprintf(result, 1024, "AI model error: %s", response->error);
            }
            ai_model_free_response(response);
        } else {
            snprintf(result, 1024, "Error: Failed to get response from AI model");
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

    // Register default tools
    agent_register_tool("calculator", "Perform basic arithmetic calculations", tool_calculator);
    agent_register_tool("time", "Get current time", tool_time);
    agent_register_tool("reverse_string", "Reverse a string", tool_reverse_string);

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
        g_agent.tool_capacity = 10;
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
