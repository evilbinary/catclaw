#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>

#include "tool.h"
 #include "common/cJSON.h"

// Create a tool
Tool* tool_create(const char* name, const char* description, const char* parameters_schema, int (*execute)(const char* args, char** result, int* result_len)) {
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
int tool_calculate(const char* args, char** result, int* result_len) {
    if (!args) {
        *result = strdup("Error: No expression provided");
        *result_len = strlen(*result);
        return -1;
    }
    
    // Simple expression evaluation (basic arithmetic)
    double calc_result = 0;
    char* expr = strdup(args);
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
int tool_get_time(const char* args, char** result, int* result_len) {
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
int tool_reverse_string(const char* args, char** result, int* result_len) {
    if (!args) {
        *result = strdup("Error: No string provided");
        *result_len = strlen(*result);
        return -1;
    }
    
    int length = strlen(args);
    char* reversed = (char*)malloc(length + 1);
    if (!reversed) {
        *result = strdup("Error: Memory allocation failed");
        *result_len = strlen(*result);
        return -1;
    }
    
    for (int i = 0; i < length; i++) {
        reversed[i] = args[length - 1 - i];
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
int tool_read_file(const char* args, char** result, int* result_len) {
    if (!args) {
        *result = strdup("Error: No file path provided");
        *result_len = strlen(*result);
        return -1;
    }
    
    FILE* fp = fopen(args, "r");
    if (!fp) {
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
        *result = strdup("Error: Memory allocation failed");
        *result_len = strlen(*result);
        return -1;
    }
    
    fread(*result, 1, length, fp);
    (*result)[length] = '\0';
    *result_len = length;
    fclose(fp);
    
    return 0;
}

// Write file tool
int tool_write_file(const char* args, char** result, int* result_len) {
    if (!args) {
        *result = strdup("Error: No parameters provided");
        *result_len = strlen(*result);
        return -1;
    }
    
    // Parse JSON parameters
    cJSON* root = cJSON_Parse(args);
    if (!root) {
        *result = strdup("Error: Invalid JSON parameters");
        *result_len = strlen(*result);
        return -1;
    }
    
    cJSON* path_obj = cJSON_GetObjectItem(root, "path");
    cJSON* content_obj = cJSON_GetObjectItem(root, "content");
    
    if (!path_obj || !cJSON_IsString(path_obj) || !content_obj || !cJSON_IsString(content_obj)) {
        cJSON_Delete(root);
        *result = strdup("Error: Missing or invalid path/content parameters");
        *result_len = strlen(*result);
        return -1;
    }
    
    const char* path = path_obj->valuestring;
    const char* content = content_obj->valuestring;
    
    FILE* fp = fopen(path, "w");
    if (!fp) {
        cJSON_Delete(root);
        *result = strdup("Error: Could not open file for writing");
        *result_len = strlen(*result);
        return -1;
    }
    
    fprintf(fp, "%s", content);
    fclose(fp);
    cJSON_Delete(root);
    
    *result = strdup("File written successfully");
    *result_len = strlen(*result);
    return 0;
}

// Search web tool (mock)
int tool_search_web(const char* args, char** result, int* result_len) {
    if (!args) {
        *result = strdup("Error: No search query provided");
        *result_len = strlen(*result);
        return -1;
    }
    
    *result = (char*)malloc(200);
    if (*result) {
        snprintf(*result, 200, "Search results for '%s':\n1. Result 1\n2. Result 2\n3. Result 3", args);
        *result_len = strlen(*result);
    }
    
    return 0;
}

// Save memory tool
int tool_save_memory(const char* args, char** result, int* result_len) {
    if (!args) {
        *result = strdup("Error: No parameters provided");
        *result_len = strlen(*result);
        return -1;
    }
    
    // Parse JSON parameters
    cJSON* root = cJSON_Parse(args);
    if (!root) {
        *result = strdup("Error: Invalid JSON parameters");
        *result_len = strlen(*result);
        return -1;
    }
    
    cJSON* key_obj = cJSON_GetObjectItem(root, "key");
    cJSON* value_obj = cJSON_GetObjectItem(root, "value");
    
    if (!key_obj || !cJSON_IsString(key_obj) || !value_obj || !cJSON_IsString(value_obj)) {
        cJSON_Delete(root);
        *result = strdup("Error: Missing or invalid key/value parameters");
        *result_len = strlen(*result);
        return -1;
    }
    
    const char* key = key_obj->valuestring;
    const char* value = value_obj->valuestring;
    
    // Here we would normally save to memory storage
    // For now, just return success
    cJSON_Delete(root);
    
    *result = (char*)malloc(100);
    if (*result) {
        snprintf(*result, 100, "Saved memory: %s = %s", key, value);
        *result_len = strlen(*result);
    }
    
    return 0;
}

// Read memory tool
int tool_read_memory(const char* args, char** result, int* result_len) {
    if (!args) {
        *result = strdup("Error: No key provided");
        *result_len = strlen(*result);
        return -1;
    }
    
    // Here we would normally read from memory storage
    // For now, just return a mock value
    *result = (char*)malloc(100);
    if (*result) {
        snprintf(*result, 100, "Memory value for '%s': [mock value]", args);
        *result_len = strlen(*result);
    }
    
    return 0;
}

// Weather tool
int tool_get_weather(const char* args, char** result, int* result_len) {
    if (!args || strlen(args) == 0) {
        *result = strdup("Error: No location provided");
        *result_len = strlen(*result);
        return -1;
    }
    
    // Parse JSON parameters to get location
    cJSON* root = cJSON_Parse(args);
    const char* location = args;
    
    if (root) {
        cJSON* location_obj = cJSON_GetObjectItem(root, "location");
        if (location_obj && cJSON_IsString(location_obj)) {
            location = location_obj->valuestring;
        }
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
    
    if (root) {
        cJSON_Delete(root);
    }
    
    return 0;
}

// List directory tool
int tool_list_directory(const char* args, char** result, int* result_len) {
    if (!args || strlen(args) == 0) {
        *result = strdup("Error: No path provided");
        *result_len = strlen(*result);
        return -1;
    }
    
    // Debug: print raw args
    fprintf(stderr, "[DEBUG] list_directory args: %s\n", args);
    
    // Parse JSON parameters to get path
    cJSON* root = cJSON_Parse(args);
    char clean_path[512];
    
    if (root) {
        // Successfully parsed as JSON
        cJSON* path_obj = cJSON_GetObjectItem(root, "path");
        if (path_obj && cJSON_IsString(path_obj)) {
            strncpy(clean_path, path_obj->valuestring, sizeof(clean_path) - 1);
            clean_path[sizeof(clean_path) - 1] = '\0';
            fprintf(stderr, "[DEBUG] Extracted path from JSON: %s\n", clean_path);
        } else {
            // JSON parsed but no path field
            snprintf(clean_path, sizeof(clean_path), "Error: No 'path' field in JSON: %s", args);
            *result = strdup(clean_path);
            *result_len = strlen(*result);
            cJSON_Delete(root);
            return -1;
        }
        cJSON_Delete(root);
    } else {
        // Not valid JSON, treat as plain path
        strncpy(clean_path, args, sizeof(clean_path) - 1);
        clean_path[sizeof(clean_path) - 1] = '\0';
        fprintf(stderr, "[DEBUG] Using args as plain path: %s\n", clean_path);
    }
    
    // If path is "." or empty, use current directory
    if (strcmp(clean_path, "") == 0 || strcmp(clean_path, "\"") == 0) {
        strcpy(clean_path, ".");
    }
    
    // Remove quotes if present
    size_t len = strlen(clean_path);
    if (len > 1 && clean_path[0] == '"' && clean_path[len-1] == '"') {
        clean_path[len-1] = '\0';
        memmove(clean_path, clean_path + 1, len - 1);
    }
    
    // Trim whitespace
    char* start = clean_path;
    while (*start == ' ' || *start == '\t') start++;
    char* end = clean_path + strlen(clean_path) - 1;
    while (end > start && (*end == ' ' || *end == '\t')) *end-- = '\0';
    if (start != clean_path) {
        memmove(clean_path, start, strlen(start) + 1);
    }
    
    fprintf(stderr, "[DEBUG] Final path: %s\n", clean_path);
    
    DIR* dir = opendir(clean_path);
    if (!dir) {
        *result = (char*)malloc(256);
        if (*result) {
            snprintf(*result, 256, "Error: Cannot open directory '%s': %s", clean_path, strerror(errno));
            *result_len = strlen(*result);
        }
        if (root) cJSON_Delete(root);
        return -1;
    }
    
    // Allocate buffer for result
    size_t buffer_size = 4096;
    *result = (char*)malloc(buffer_size);
    if (!*result) {
        closedir(dir);
        if (root) cJSON_Delete(root);
        return -1;
    }
    
    snprintf(*result, buffer_size, "Directory listing of '%s':\n", clean_path);
    
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
                if (root) cJSON_Delete(root);
                return -1;
            }
            *result = new_result;
        }
        
        // Append entry type indicator using stat
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", clean_path, entry->d_name);
        
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
    
    if (count >= 50) {
        strcat(*result, "... (more items)\n");
    }
    
    *result_len = strlen(*result);
    
    if (root) {
        cJSON_Delete(root);
    }
    
    return 0;
}
