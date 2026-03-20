#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>

#include "tool.h"
#include "common/cJSON.h"

// Resolve relative path to absolute path
// If path starts with '/', return as-is (absolute path)
// If path starts with '~', expand to home directory
// Otherwise, resolve relative to current working directory
static char* resolve_path(const char* path) {
    if (!path) return NULL;
    
    // Absolute path - return copy
    if (path[0] == '/') {
        return strdup(path);
    }
    
    // Home directory expansion
    if (path[0] == '~') {
        const char* home = getenv("HOME");
        if (!home) {
            struct passwd* pw = getpwuid(getuid());
            if (pw) home = pw->pw_dir;
        }
        if (home) {
            size_t home_len = strlen(home);
            size_t path_len = strlen(path);
            char* resolved = malloc(home_len + path_len + 1);
            if (!resolved) return NULL;
            strcpy(resolved, home);
            strcat(resolved, path + 1);  // Skip the '~'
            return resolved;
        }
    }
    
    // Relative path - resolve against current working directory
    char cwd[1024];
    if (!getcwd(cwd, sizeof(cwd))) {
        return strdup(path);  // Fallback to original path
    }
    
    size_t cwd_len = strlen(cwd);
    size_t path_len = strlen(path);
    
    char* resolved = malloc(cwd_len + path_len + 2);
    if (!resolved) return NULL;
    
    strcpy(resolved, cwd);
    if (cwd_len > 0 && cwd[cwd_len - 1] != '/') {
        strcat(resolved, "/");
    }
    strcat(resolved, path);
    
    return resolved;
}

// Helper to get argument value by key
const char* tool_args_get(ToolArgs* args, const char* key) {
    if (!args || !key) return NULL;
    for (int i = 0; i < args->count; i++) {
        if (args->args[i].key && strcmp(args->args[i].key, key) == 0) {
            return args->args[i].value;
        }
    }
    return NULL;
}

// Helper to create ToolArgs from a single string argument
ToolArgs* tool_args_from_string(const char* value) {
    ToolArgs* args = (ToolArgs*)malloc(sizeof(ToolArgs));
    if (!args) return NULL;
    
    args->count = 1;
    args->args = (ToolArg*)malloc(sizeof(ToolArg));
    if (!args->args) {
        free(args);
        return NULL;
    }
    
    args->args[0].key = strdup("arg");
    args->args[0].value = value ? strdup(value) : strdup("");
    
    return args;
}

// Helper to free ToolArgs
void tool_args_free(ToolArgs* args) {
    if (!args) return;
    for (int i = 0; i < args->count; i++) {
        free(args->args[i].key);
        free(args->args[i].value);
    }
    free(args->args);
    free(args);
}

// Create a tool
Tool* tool_create(const char* name, const char* description, const char* parameters_schema, int (*execute)(ToolArgs* args, char** result, int* result_len)) {
    Tool* tool = (Tool*)malloc(sizeof(Tool));
    if (!tool) {
        return NULL;
    }
    
    tool->name = strdup(name);
    tool->description = strdup(description);
    tool->parameters_schema = parameters_schema ? strdup(parameters_schema) : NULL;
    tool->execute = execute;
    
    return tool;
}

// Destroy a tool
void tool_destroy(Tool* tool) {
    if (tool) {
        if (tool->name) {
            free(tool->name);
        }
        if (tool->description) {
            free(tool->description);
        }
        if (tool->parameters_schema) {
            free(tool->parameters_schema);
        }
        free(tool);
    }
}

// Initialize tool registry
ToolRegistry* tool_registry_init(void) {
    ToolRegistry* registry = (ToolRegistry*)malloc(sizeof(ToolRegistry));
    if (!registry) {
        return NULL;
    }
    
    registry->capacity = 20;
    registry->count = 0;
    registry->tools = (Tool**)malloc(sizeof(Tool*) * registry->capacity);
    if (!registry->tools) {
        free(registry);
        return NULL;
    }
    
    return registry;
}

// Destroy tool registry
void tool_registry_destroy(ToolRegistry* registry) {
    if (registry) {
        if (registry->tools) {
            for (int i = 0; i < registry->count; i++) {
                tool_destroy(registry->tools[i]);
            }
            free(registry->tools);
        }
        free(registry);
    }
}

// Register a tool
bool tool_registry_register(ToolRegistry* registry, Tool* tool) {
    if (!registry || !tool) {
        return false;
    }
    
    // Check if tool already exists
    for (int i = 0; i < registry->count; i++) {
        if (strcmp(registry->tools[i]->name, tool->name) == 0) {
            return false;
        }
    }
    
    // Resize if needed
    if (registry->count >= registry->capacity) {
        int new_capacity = registry->capacity * 2;
        Tool** new_tools = (Tool**)realloc(registry->tools, sizeof(Tool*) * new_capacity);
        if (!new_tools) {
            return false;
        }
        registry->tools = new_tools;
        registry->capacity = new_capacity;
    }
    
    registry->tools[registry->count++] = tool;
    return true;
}

// Get a tool by name
Tool* tool_registry_get(ToolRegistry* registry, const char* name) {
    if (!registry || !name) {
        return NULL;
    }
    
    for (int i = 0; i < registry->count; i++) {
        if (strcmp(registry->tools[i]->name, name) == 0) {
            return registry->tools[i];
        }
    }
    
    return NULL;
}

// List all tools
void tool_registry_list(ToolRegistry* registry) {
    if (!registry) {
        return;
    }
    
    printf("Available tools:\n");
    for (int i = 0; i < registry->count; i++) {
        Tool* tool = registry->tools[i];
        printf("  %s: %s\n", tool->name, tool->description);
        if (tool->parameters_schema) {
            printf("    Parameters schema: %s\n", tool->parameters_schema);
        }
    }
}

// Calculate tool
int tool_calculate(ToolArgs* args, char** result, int* result_len) {
    const char* expr_str = tool_args_get(args, "expression");
    if (!expr_str) expr_str = tool_args_get(args, "arg");
    
    if (!expr_str) {
        *result = strdup("Error: No expression provided");
        *result_len = strlen(*result);
        return -1;
    }
    
    // Simple expression evaluation (basic arithmetic)
    double calc_result = 0;
    char* expr = strdup(expr_str);
    if (!expr) {
        *result = strdup("Error: Memory allocation failed");
        *result_len = strlen(*result);
        return -1;
    }
    
    // Remove spaces
    char* clean_expr = expr;
    char* write_ptr = expr;
    while (*clean_expr) {
        if (*clean_expr != ' ') {
            *write_ptr++ = *clean_expr;
        }
        clean_expr++;
    }
    *write_ptr = '\0';
    
    // Use sscanf to parse and evaluate
    double a, b;
    if (sscanf(expr, "%lf+%lf", &a, &b) == 2) {
        calc_result = a + b;
    } else if (sscanf(expr, "%lf-%lf", &a, &b) == 2) {
        calc_result = a - b;
    } else if (sscanf(expr, "%lf*%lf", &a, &b) == 2) {
        calc_result = a * b;
    } else if (sscanf(expr, "%lf/%lf", &a, &b) == 2) {
        if (b == 0) {
            free(expr);
            *result = strdup("Error: Division by zero");
            *result_len = strlen(*result);
            return -1;
        }
        calc_result = a / b;
    } else {
        free(expr);
        *result = strdup("Error: Invalid expression");
        *result_len = strlen(*result);
        return -1;
    }
    
    *result = (char*)malloc(50);
    if (*result) {
        snprintf(*result, 50, "Result: %.2f", calc_result);
        *result_len = strlen(*result);
    }
    
    free(expr);
    return 0;
}

// Get time tool
int tool_get_time(ToolArgs* args, char** result, int* result_len) {
    (void)args;  // Unused parameter
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    
    *result = (char*)malloc(100);
    if (*result) {
        strftime(*result, 100, "Current time: %Y-%m-%d %H:%M:%S", tm_info);
        *result_len = strlen(*result);
    }
    
    return 0;
}

// Reverse string tool
int tool_reverse_string(ToolArgs* args, char** result, int* result_len) {
    const char* text = tool_args_get(args, "text");
    if (!text) text = tool_args_get(args, "arg");
    
    if (!text) {
        *result = strdup("Error: No string provided");
        *result_len = strlen(*result);
        return -1;
    }
    
    int length = strlen(text);
    char* reversed = (char*)malloc(length + 1);
    if (!reversed) {
        *result = strdup("Error: Memory allocation failed");
        *result_len = strlen(*result);
        return -1;
    }
    
    for (int i = 0; i < length; i++) {
        reversed[i] = text[length - 1 - i];
    }
    reversed[length] = '\0';
    
    *result = (char*)malloc(length + 20);
    if (*result) {
        snprintf(*result, length + 20, "Reversed string: %s", reversed);
        *result_len = strlen(*result);
    }
    
    free(reversed);
    return 0;
}

// Read file tool
int tool_read_file(ToolArgs* args, char** result, int* result_len) {
    const char* path = tool_args_get(args, "path");
    if (!path) path = tool_args_get(args, "arg");
    
    if (!path || strlen(path) == 0) {
        *result = strdup("Error: No file path provided");
        *result_len = strlen(*result);
        return -1;
    }
    
    char* resolved_path = resolve_path(path);
    if (!resolved_path) {
        *result = strdup("Error: Failed to resolve path");
        *result_len = strlen(*result);
        return -1;
    }
    
    FILE* fp = fopen(resolved_path, "r");
    if (!fp) {
        free(resolved_path);
        *result = strdup("Error: Could not open file");
        *result_len = strlen(*result);
        return -1;
    }
    
    // Get file size
    fseek(fp, 0, SEEK_END);
    long length = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    *result = (char*)malloc(length + 1);
    if (!*result) {
        fclose(fp);
        free(resolved_path);
        *result = strdup("Error: Memory allocation failed");
        *result_len = strlen(*result);
        return -1;
    }
    
    fread(*result, 1, length, fp);
    (*result)[length] = '\0';
    *result_len = length;
    fclose(fp);
    free(resolved_path);
    
    return 0;
}

// Write file tool
int tool_write_file(ToolArgs* args, char** result, int* result_len) {
    const char* path = tool_args_get(args, "path");
    const char* content = tool_args_get(args, "content");
    
    if (!path || !content) {
        *result = strdup("Error: Missing path or content parameter");
        *result_len = strlen(*result);
        return -1;
    }
    
    char* resolved_path = resolve_path(path);
    if (!resolved_path) {
        *result = strdup("Error: Failed to resolve path");
        *result_len = strlen(*result);
        return -1;
    }
    
    FILE* fp = fopen(resolved_path, "w");
    if (!fp) {
        free(resolved_path);
        *result = strdup("Error: Could not open file for writing");
        *result_len = strlen(*result);
        return -1;
    }
    
    fprintf(fp, "%s", content);
    fclose(fp);
    free(resolved_path);
    
    *result = strdup("File written successfully");
    *result_len = strlen(*result);
    return 0;
}

// Search web tool (mock)
int tool_search_web(ToolArgs* args, char** result, int* result_len) {
    const char* query = tool_args_get(args, "query");
    if (!query) query = tool_args_get(args, "arg");
    
    if (!query) {
        *result = strdup("Error: No search query provided");
        *result_len = strlen(*result);
        return -1;
    }
    
    *result = (char*)malloc(200);
    if (*result) {
        snprintf(*result, 200, "Search results for '%s':\n1. Result 1\n2. Result 2\n3. Result 3", query);
        *result_len = strlen(*result);
    }
    
    return 0;
}

// Save memory tool
int tool_save_memory(ToolArgs* args, char** result, int* result_len) {
    const char* key = tool_args_get(args, "key");
    const char* value = tool_args_get(args, "value");
    
    if (!key || !value) {
        *result = strdup("Error: Missing key or value parameter");
        *result_len = strlen(*result);
        return -1;
    }
    
    // Here we would normally save to memory storage
    // For now, just return success
    *result = (char*)malloc(100);
    if (*result) {
        snprintf(*result, 100, "Saved memory: %s = %s", key, value);
        *result_len = strlen(*result);
    }
    
    return 0;
}

// Read memory tool
int tool_read_memory(ToolArgs* args, char** result, int* result_len) {
    const char* key = tool_args_get(args, "key");
    if (!key) key = tool_args_get(args, "arg");
    
    if (!key) {
        *result = strdup("Error: No key provided");
        *result_len = strlen(*result);
        return -1;
    }
    
    // Here we would normally read from memory storage
    // For now, just return a mock value
    *result = (char*)malloc(100);
    if (*result) {
        snprintf(*result, 100, "Memory value for '%s': [mock value]", key);
        *result_len = strlen(*result);
    }
    
    return 0;
}

// Weather tool
int tool_get_weather(ToolArgs* args, char** result, int* result_len) {
    const char* location = tool_args_get(args, "location");
    if (!location) location = tool_args_get(args, "arg");
    
    if (!location || strlen(location) == 0) {
        *result = strdup("Error: No location provided");
        *result_len = strlen(*result);
        return -1;
    }
    
    *result = (char*)malloc(256);
    if (*result) {
        // Simple mock weather data
        if (strstr(location, "beijing") || strstr(location, "北京")) {
            snprintf(*result, 256, "北京天气：22°C，晴朗，湿度：45%%");
        } else if (strstr(location, "shanghai") || strstr(location, "上海")) {
            snprintf(*result, 256, "上海天气：25°C，多云，湿度：60%%");
        } else if (strstr(location, "guangzhou") || strstr(location, "广州")) {
            snprintf(*result, 256, "广州天气：28°C，雷阵雨，湿度：75%%");
        } else if (strstr(location, "shenzhen") || strstr(location, "深圳")) {
            snprintf(*result, 256, "深圳天气：27°C，多云，湿度：70%%");
        } else if (strstr(location, "new york") || strstr(location, "纽约")) {
            snprintf(*result, 256, "纽约天气：18°C，小雨，湿度：75%%");
        } else if (strstr(location, "london") || strstr(location, "伦敦")) {
            snprintf(*result, 256, "伦敦天气：15°C，雾，湿度：80%%");
        } else if (strstr(location, "tokyo") || strstr(location, "东京")) {
            snprintf(*result, 256, "东京天气：20°C，晴间多云，湿度：55%%");
        } else {
            snprintf(*result, 256, "%s天气：20°C，局部多云，湿度：50%%", location);
        }
        *result_len = strlen(*result);
    }
    
    return 0;
}

// List directory tool
int tool_list_directory(ToolArgs* args, char** result, int* result_len) {
    const char* path = tool_args_get(args, "path");
    if (!path) path = tool_args_get(args, "arg");
    
    if (!path || strlen(path) == 0) {
        path = ".";
    }
    
    char* resolved_path = resolve_path(path);
    if (!resolved_path) {
        *result = strdup("Error: Failed to resolve path");
        *result_len = strlen(*result);
        return -1;
    }
    
    DIR* dir = opendir(resolved_path);
    if (!dir) {
        *result = (char*)malloc(256);
        if (*result) {
            snprintf(*result, 256, "Error: Cannot open directory '%s': %s", resolved_path, strerror(errno));
            *result_len = strlen(*result);
        }
        free(resolved_path);
        return -1;
    }
    
    // Allocate buffer for result
    size_t buffer_size = 4096;
    *result = (char*)malloc(buffer_size);
    if (!*result) {
        closedir(dir);
        free(resolved_path);
        return -1;
    }
    
    snprintf(*result, buffer_size, "Directory listing of '%s':\n", resolved_path);
    
    struct dirent* entry;
    int count = 0;
    while ((entry = readdir(dir)) != NULL && count < 50) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        // Check if we need to expand buffer
        size_t current_len = strlen(*result);
        size_t needed = current_len + strlen(entry->d_name) + 20;
        if (needed > buffer_size) {
            buffer_size *= 2;
            char* new_result = (char*)realloc(*result, buffer_size);
            if (!new_result) {
                free(*result);
                closedir(dir);
                free(resolved_path);
                return -1;
            }
            *result = new_result;
        }
        
        // Append entry type indicator using stat
        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", resolved_path, entry->d_name);
        
        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                strcat(*result, "[DIR]  ");
            } else {
                strcat(*result, "[FILE] ");
            }
        } else {
            strcat(*result, "[?]    ");
        }
        strcat(*result, entry->d_name);
        strcat(*result, "\n");
        count++;
    }
    
    closedir(dir);
    free(resolved_path);
    
    if (count >= 50) {
        strcat(*result, "... (more items)\n");
    }
    
    *result_len = strlen(*result);
    
    return 0;
}
