#include "common/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#ifndef _WIN32
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <sys/select.h>
#include <signal.h>
#include <fcntl.h>
#include <pwd.h>
#endif

#include "tool.h"
#include "skill.h"
#include "common/cJSON.h"
#include "common/http_client.h"
#include "common/log.h"
#include "common/utils.h"

// Forward declarations for agent memory functions
// These are implemented in agent/agent.c
extern bool agent_memory_set(const char *key, const char *value);
extern char *agent_memory_get(const char *key);
extern bool agent_memory_clear(void);
extern bool agent_memory_delete(const char *key);

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

// Search web tool - using DuckDuckGo Instant Answer API
int tool_search_web(ToolArgs* args, char** result, int* result_len) {
    const char* query = tool_args_get(args, "query");
    if (!query) query = tool_args_get(args, "arg");

    if (!query) {
        *result = strdup("Error: No search query provided");
        *result_len = strlen(*result);
        return -1;
    }

    // URL encode the query
    char* encoded_query = http_url_encode(query);
    if (!encoded_query) {
        *result = strdup("Error: Failed to encode query");
        *result_len = strlen(*result);
        return -1;
    }

    // Build DuckDuckGo API URL
    char url[512];
    snprintf(url, sizeof(url),
             "https://api.duckduckgo.com/?q=%s&format=json&no_html=1&skip_disambig=1",
             encoded_query);
    free(encoded_query);

    // Make HTTP request
    HttpRequest req = {
        .url = url,
        .method = "GET",
        .timeout_sec = 15,
        .follow_redirects = true
    };

    HttpResponse* resp = http_request(&req);
    if (!resp || !resp->success) {
        if (resp) http_response_free(resp);
        *result = strdup("Error: Failed to connect to search API");
        *result_len = strlen(*result);
        return -1;
    }

    // Parse JSON response
    cJSON* json = cJSON_Parse(resp->body);
    http_response_free(resp);

    if (!json) {
        *result = strdup("Error: Failed to parse search response");
        *result_len = strlen(*result);
        return -1;
    }

    // Build result string
    size_t buffer_size = 4096;
    *result = (char*)malloc(buffer_size);
    if (!*result) {
        cJSON_Delete(json);
        return -1;
    }

    int offset = snprintf(*result, buffer_size, "🔍 搜索结果: %s\n\n", query);

    // Get abstract (main answer)
    cJSON* abstract = cJSON_GetObjectItem(json, "Abstract");
    cJSON* abstract_url = cJSON_GetObjectItem(json, "AbstractURL");
    cJSON* abstract_source = cJSON_GetObjectItem(json, "AbstractSource");

    if (abstract && strlen(abstract->valuestring) > 0) {
        offset += snprintf(*result + offset, buffer_size - offset,
                          "📌 摘要:\n%s\n", abstract->valuestring);
        if (abstract_url && strlen(abstract_url->valuestring) > 0) {
            offset += snprintf(*result + offset, buffer_size - offset,
                              "🔗 来源: %s", abstract_url->valuestring);
            if (abstract_source && strlen(abstract_source->valuestring) > 0) {
                offset += snprintf(*result + offset, buffer_size - offset,
                                  " (%s)", abstract_source->valuestring);
            }
            offset += snprintf(*result + offset, buffer_size - offset, "\n");
        }
        offset += snprintf(*result + offset, buffer_size - offset, "\n");
    }

    // Get related topics
    cJSON* related_topics = cJSON_GetObjectItem(json, "RelatedTopics");
    if (related_topics && cJSON_IsArray(related_topics)) {
        int topic_count = cJSON_GetArraySize(related_topics);
        int shown = 0;

        offset += snprintf(*result + offset, buffer_size - offset, "📚 相关主题:\n");

        for (int i = 0; i < topic_count && shown < 5; i++) {
            cJSON* topic = cJSON_GetArrayItem(related_topics, i);
            cJSON* text = cJSON_GetObjectItem(topic, "Text");
            cJSON* first_url = cJSON_GetObjectItem(topic, "FirstURL");

            // Skip if this is a nested Topics array
            if (!text && cJSON_GetObjectItem(topic, "Topics")) {
                cJSON* nested_topics = cJSON_GetObjectItem(topic, "Topics");
                if (nested_topics && cJSON_IsArray(nested_topics)) {
                    int nested_count = cJSON_GetArraySize(nested_topics);
                    for (int j = 0; j < nested_count && shown < 5; j++) {
                        cJSON* nested = cJSON_GetArrayItem(nested_topics, j);
                        cJSON* n_text = cJSON_GetObjectItem(nested, "Text");
                        cJSON* n_url = cJSON_GetObjectItem(nested, "FirstURL");

                        if (n_text && strlen(n_text->valuestring) > 0) {
                            offset += snprintf(*result + offset, buffer_size - offset,
                                             "%d. %s\n", shown + 1, n_text->valuestring);
                            if (n_url && strlen(n_url->valuestring) > 0) {
                                offset += snprintf(*result + offset, buffer_size - offset,
                                                 "   %s\n", n_url->valuestring);
                            }
                            shown++;
                        }
                    }
                }
                continue;
            }

            if (text && strlen(text->valuestring) > 0) {
                offset += snprintf(*result + offset, buffer_size - offset,
                                 "%d. %s\n", shown + 1, text->valuestring);
                if (first_url && strlen(first_url->valuestring) > 0) {
                    offset += snprintf(*result + offset, buffer_size - offset,
                                     "   %s\n", first_url->valuestring);
                }
                shown++;
            }
        }
    }

    // Check if we got any results
    if (offset <= (int)strlen(query) + 20) {
        offset = snprintf(*result, buffer_size,
                         "🔍 搜索结果: %s\n\n"
                         "未找到相关结果。\n"
                         "建议尝试其他关键词或检查网络连接。",
                         query);
    }

    cJSON_Delete(json);
    *result_len = offset;
    return 0;
}

// Save memory tool
int tool_save_memory(ToolArgs* args, char** result, int* result_len) {
    const char* key = tool_args_get(args, "key");
    const char* value = tool_args_get(args, "value");
    
    // If key/value not provided as separate args, try to parse from "arg"
    if (!key || !value) {
        const char* arg = tool_args_get(args, "arg");
        if (arg) {
            // Parse "key value" format - first word is key, rest is value
            char* arg_copy = strdup(arg);
            char* space = strchr(arg_copy, ' ');
            if (space) {
                *space = '\0';
                key = arg_copy;
                value = space + 1;
            } else {
                free(arg_copy);
                *result = strdup("Error: Invalid format. Use 'memory save <key> <value>'");
                *result_len = strlen(*result);
                return -1;
            }
        }
    }
    
    if (!key || !value) {
        *result = strdup("Error: Missing key or value parameter");
        *result_len = strlen(*result);
        return -1;
    }
    
    // Save to memory storage using agent's memory interface
    if (agent_memory_set(key, value)) {
        size_t buf_size = strlen(key) + strlen(value) + 100;
        *result = (char*)malloc(buf_size);
        if (*result) {
            snprintf(*result, buf_size, "✓ Saved to memory: %s = %s", key, value);
            *result_len = strlen(*result);
        }
        return 0;
    } else {
        *result = strdup("Error: Failed to save memory (storage may be full)");
        *result_len = strlen(*result);
        return -1;
    }
}

// Read memory tool
int tool_read_memory(ToolArgs* args, char** result, int* result_len) {
    const char* key = tool_args_get(args, "key");
    if (!key) key = tool_args_get(args, "arg");
    
    // If no key provided, list all keys (TODO: implement list in agent)
    if (!key) {
        *result = strdup("Memory list not implemented. Please provide a key.");
        *result_len = strlen(*result);
        return 0;
    }
    
    // Read from memory storage using agent's memory interface
    char* value = agent_memory_get(key);
    if (value) {
        size_t buf_size = strlen(key) + strlen(value) + 100;
        *result = (char*)malloc(buf_size);
        if (*result) {
            snprintf(*result, buf_size, "✓ Memory value for '%s': %s", key, value);
            *result_len = strlen(*result);
        }
        free(value);  // agent_memory_get returns strdup'd string
        return 0;
    } else {
        size_t buf_size = strlen(key) + 100;
        *result = (char*)malloc(buf_size);
        if (*result) {
            snprintf(*result, buf_size, "✗ No memory found for key: %s", key);
            *result_len = strlen(*result);
        }
        return 0;
    }
}

// Delete memory tool
int tool_delete_memory(ToolArgs* args, char** result, int* result_len) {
    const char* key = tool_args_get(args, "key");
    if (!key) key = tool_args_get(args, "arg");
    
    if (!key) {
        *result = strdup("Error: No key provided");
        *result_len = strlen(*result);
        return -1;
    }
    
    // Use agent's memory delete function
    if (agent_memory_delete(key)) {
        size_t buf_size = strlen(key) + 100;
        *result = (char*)malloc(buf_size);
        if (*result) {
            snprintf(*result, buf_size, "✓ Deleted memory key: %s", key);
            *result_len = strlen(*result);
        }
        return 0;
    } else {
        size_t buf_size = strlen(key) + 100;
        *result = (char*)malloc(buf_size);
        if (*result) {
            snprintf(*result, buf_size, "✗ Key not found: %s", key);
            *result_len = strlen(*result);
        }
        return 0;
    }
}

// Weather tool - calls wttr.in API
int tool_get_weather(ToolArgs* args, char** result, int* result_len) {
    const char* location = tool_args_get(args, "location");
    if (!location) location = tool_args_get(args, "arg");
    
    if (!location || strlen(location) == 0) {
        *result = strdup("Error: No location provided");
        *result_len = strlen(*result);
        return -1;
    }
    
    // Build URL for wttr.in API (format=j1 for JSON)
    char url[512];
    char* encoded_location = http_url_encode(location);
    if (!encoded_location) {
        *result = strdup("Error: Failed to encode location");
        *result_len = strlen(*result);
        return -1;
    }
    snprintf(url, sizeof(url), "https://wttr.in/%s?format=j1", encoded_location);
    free(encoded_location);
    
    // Make HTTP request
    HttpResponse* resp = http_get(url);
    if (!resp || !resp->success) {
        if (resp) http_response_free(resp);
        *result = strdup("Error: Failed to fetch weather data");
        *result_len = strlen(*result);
        return -1;
    }
    
    // Parse JSON response
    cJSON* root = cJSON_Parse(resp->body);
    http_response_free(resp);
    
    if (!root) {
        *result = strdup("Error: Failed to parse weather data");
        *result_len = strlen(*result);
        return -1;
    }
    
    // Extract weather info from current_condition array
    cJSON* current = cJSON_GetObjectItem(root, "current_condition");
    cJSON* area = cJSON_GetObjectItem(root, "nearest_area");
    
    char* weather_desc = NULL;
    char* temp_c = NULL;
    char* humidity = NULL;
    char* feels_like = NULL;
    char* wind_speed = NULL;
    char* area_name = NULL;
    
    if (current && cJSON_IsArray(current) && cJSON_GetArraySize(current) > 0) {
        cJSON* cond = cJSON_GetArrayItem(current, 0);
        if (cond) {
            cJSON* desc_arr = cJSON_GetObjectItem(cond, "weatherDesc");
            if (desc_arr && cJSON_IsArray(desc_arr) && cJSON_GetArraySize(desc_arr) > 0) {
                cJSON* desc_item = cJSON_GetArrayItem(desc_arr, 0);
                if (desc_item) {
                    cJSON* val = cJSON_GetObjectItem(desc_item, "value");
                    if (val && cJSON_IsString(val)) {
                        weather_desc = strdup(val->valuestring);
                    }
                }
            }
            cJSON* temp = cJSON_GetObjectItem(cond, "temp_C");
            if (temp && cJSON_IsString(temp)) {
                temp_c = strdup(temp->valuestring);
            }
            cJSON* hum = cJSON_GetObjectItem(cond, "humidity");
            if (hum && cJSON_IsString(hum)) {
                humidity = strdup(hum->valuestring);
            }
            cJSON* feels = cJSON_GetObjectItem(cond, "FeelsLikeC");
            if (feels && cJSON_IsString(feels)) {
                feels_like = strdup(feels->valuestring);
            }
            cJSON* wind = cJSON_GetObjectItem(cond, "windspeedKmph");
            if (wind && cJSON_IsString(wind)) {
                wind_speed = strdup(wind->valuestring);
            }
        }
    }
    
    // Get area name
    if (area && cJSON_IsArray(area) && cJSON_GetArraySize(area) > 0) {
        cJSON* area_obj = cJSON_GetArrayItem(area, 0);
        if (area_obj) {
            cJSON* name_arr = cJSON_GetObjectItem(area_obj, "areaName");
            if (name_arr && cJSON_IsArray(name_arr) && cJSON_GetArraySize(name_arr) > 0) {
                cJSON* name_item = cJSON_GetArrayItem(name_arr, 0);
                if (name_item) {
                    cJSON* val = cJSON_GetObjectItem(name_item, "value");
                    if (val && cJSON_IsString(val)) {
                        area_name = strdup(val->valuestring);
                    }
                }
            }
        }
    }
    
    cJSON_Delete(root);
    
    // Format result
    size_t buf_size = 512;
    *result = (char*)malloc(buf_size);
    if (*result) {
        snprintf(*result, buf_size, "%s天气：%s，温度：%s°C (体感%s°C)，湿度：%s%%，风速：%skm/h",
            area_name ? area_name : location,
            weather_desc ? weather_desc : "未知",
            temp_c ? temp_c : "?",
            feels_like ? feels_like : "?",
            humidity ? humidity : "?",
            wind_speed ? wind_speed : "?");
        *result_len = strlen(*result);
    }
    
    free(weather_desc);
    free(temp_c);
    free(humidity);
    free(feels_like);
    free(wind_speed);
    free(area_name);
    
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
        
        // Append entry type indicator
        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", resolved_path, entry->d_name);
        
        if (platform_is_dir(full_path)) {
            strcat(*result, "[DIR]  ");
        } else {
            strcat(*result, "[FILE] ");
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

// Web fetch tool - fetch content from a URL
int tool_web_fetch(ToolArgs* args, char** result, int* result_len) {
    const char* url = tool_args_get(args, "url");
    if (!url) url = tool_args_get(args, "arg");
    
    if (!url || strlen(url) == 0) {
        *result = strdup("Error: No URL provided");
        *result_len = strlen(*result);
        return -1;
    }
    
    // Validate URL scheme
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        *result = strdup("Error: URL must start with http:// or https://");
        *result_len = strlen(*result);
        return -1;
    }
    
    // Make HTTP request
    HttpRequest req = {
        .url = url,
        .method = "GET",
        .timeout_sec = 30,
        .follow_redirects = true
    };
    
    HttpResponse* resp = http_request(&req);
    if (!resp || !resp->success) {
        char* err_msg = (char*)malloc(256);
        if (err_msg) {
            snprintf(err_msg, 256, "Error: Failed to fetch URL (status: %d)", 
                     resp ? resp->status_code : 0);
        }
        if (resp) http_response_free(resp);
        *result = err_msg ? err_msg : strdup("Error: Failed to fetch URL");
        *result_len = strlen(*result);
        return -1;
    }
    
    // Build result with metadata
    size_t body_len = resp->body_len;
    size_t header_size = 512;
    size_t result_size = header_size + body_len + 1;
    
    *result = (char*)malloc(result_size);
    if (!*result) {
        http_response_free(resp);
        *result = strdup("Error: Memory allocation failed");
        *result_len = strlen(*result);
        return -1;
    }
    
    int offset = snprintf(*result, result_size,
        "URL: %s\n"
        "Status: %d\n"
        "Content-Type: %s\n"
        "Size: %zu bytes\n\n",
        url, resp->status_code, 
        resp->content_type ? resp->content_type : "unknown",
        body_len);
    
    // Append body content (limit to reasonable size)
    size_t max_body = 8192;
    if (body_len > max_body) {
        memcpy(*result + offset, resp->body, max_body);
        offset += max_body;
        offset += snprintf(*result + offset, result_size - offset,
            "\n\n... (truncated, total %zu bytes)", body_len);
    } else if (body_len > 0) {
        memcpy(*result + offset, resp->body, body_len);
        offset += body_len;
    }
    
    (*result)[offset] = '\0';
    *result_len = offset;
    
    http_response_free(resp);
    return 0;
}

// Shell execute tool - run shell commands (cross-platform)
int tool_shell_execute(ToolArgs* args, char** result, int* result_len) {
    const char* cmd = tool_args_get(args, "command");
    if (!cmd) cmd = tool_args_get(args, "cmd");
    if (!cmd) cmd = tool_args_get(args, "arg");
    
    if (!cmd || strlen(cmd) == 0) {
        *result = strdup("Error: No command provided");
        *result_len = strlen(*result);
        return -1;
    }
    
#ifndef _WIN32
    // Unix: Security check - block dangerous commands
    const char* dangerous[] = {
        "rm -rf /", "mkfs", "dd if=", ":(){:|:&};:",
        "chmod -R 777 /", "chown -R", "> /dev/", NULL
    };
    for (int i = 0; dangerous[i]; i++) {
        if (strstr(cmd, dangerous[i])) {
            *result = strdup("Error: Command blocked for security");
            *result_len = strlen(*result);
            return -1;
        }
    }
#endif
    
    // Allocate result buffer
    size_t buf_size = 16384;
    *result = (char*)malloc(buf_size);
    if (!*result) {
        *result = strdup("Error: Memory allocation failed");
        *result_len = strlen(*result);
        return -1;
    }
    
    int offset = snprintf(*result, buf_size, "Command: %s\n\n", cmd);
    
    char full_cmd[2048];
    platform_prepare_command(cmd, full_cmd, sizeof(full_cmd));
    FILE* fp = popen(full_cmd, "r");
    
    if (!fp) {
        free(*result);
        *result = strdup("Error: Failed to execute command");
        *result_len = strlen(*result);
        return -1;
    }
    
    // Read output
    char line[1024];
    while (fgets(line, sizeof(line), fp) && offset < (int)(buf_size - 1024)) {
        offset += snprintf(*result + offset, buf_size - offset, "%s", line);
    }
    
    int exit_code = pclose(fp);
    
    if (offset == (int)strlen(cmd) + 11) {
        offset += snprintf(*result + offset, buf_size - offset, "(no output)\n");
    }
    
    offset += snprintf(*result + offset, buf_size - offset, "\nExit code: %d", exit_code);
    
    *result_len = offset;
    return 0;
}

int tool_grep_execute(ToolArgs* args, char** result, int* result_len) {
    const char* pattern = tool_args_get(args, "pattern");
    if (!pattern) pattern = tool_args_get(args, "arg");
    
    const char* path = tool_args_get(args, "path");
    if (!path) path = tool_args_get(args, "file");
    
    const char* options = tool_args_get(args, "options");
    
    if (!pattern || strlen(pattern) == 0) {
        *result = strdup("Error: No pattern provided");
        *result_len = strlen(*result);
        return -1;
    }
    
    size_t buf_size = 32768;
    *result = (char*)malloc(buf_size);
    if (!*result) {
        *result = strdup("Error: Memory allocation failed");
        *result_len = strlen(*result);
        return -1;
    }
    
    int offset = 0;
    
    char full_cmd[2048];
    if (path && strlen(path) > 0) {
        if (options && strlen(options) > 0) {
            snprintf(full_cmd, sizeof(full_cmd), "grep %s \"%s\" \"%s\"", options, pattern, path);
        } else {
            snprintf(full_cmd, sizeof(full_cmd), "grep \"%s\" \"%s\"", pattern, path);
        }
    } else {
        *result = strdup("Error: No file/path provided for grep");
        *result_len = strlen(*result);
        return -1;
    }
    
    offset = snprintf(*result, buf_size, "Command: %s\n\n", full_cmd);
    
    char platform_cmd[2048];
    platform_prepare_command(full_cmd, platform_cmd, sizeof(platform_cmd));
    FILE* fp = popen(platform_cmd, "r");
    
    if (!fp) {
        free(*result);
        *result = strdup("Error: Failed to execute grep command");
        *result_len = strlen(*result);
        return -1;
    }
    
    char line[2048];
    while (fgets(line, sizeof(line), fp) && offset < (int)(buf_size - 2048)) {
        offset += snprintf(*result + offset, buf_size - offset, "%s", line);
    }
    
    int exit_code = pclose(fp);
    
    if (offset == (int)strlen(full_cmd) + 11) {
        offset += snprintf(*result + offset, buf_size - offset, "(no matches)\n");
    }
    
    offset += snprintf(*result + offset, buf_size - offset, "\nExit code: %d", exit_code);
    
    *result_len = offset;
    return 0;
}

int tool_sed_execute(ToolArgs* args, char** result, int* result_len) {
    const char* expression = tool_args_get(args, "expression");
    if (!expression) expression = tool_args_get(args, "expr");
    if (!expression) expression = tool_args_get(args, "arg");
    
    const char* path = tool_args_get(args, "path");
    if (!path) path = tool_args_get(args, "file");
    
    const char* options = tool_args_get(args, "options");
    
    if (!expression || strlen(expression) == 0) {
        *result = strdup("Error: No sed expression provided");
        *result_len = strlen(*result);
        return -1;
    }
    
    if (!path || strlen(path) == 0) {
        *result = strdup("Error: No file/path provided for sed");
        *result_len = strlen(*result);
        return -1;
    }
    
    size_t buf_size = 32768;
    *result = (char*)malloc(buf_size);
    if (!*result) {
        *result = strdup("Error: Memory allocation failed");
        *result_len = strlen(*result);
        return -1;
    }
    
    int offset = 0;
    
    char full_cmd[2048];
    if (options && strlen(options) > 0) {
        snprintf(full_cmd, sizeof(full_cmd), "sed %s \"%s\" \"%s\"", options, expression, path);
    } else {
        snprintf(full_cmd, sizeof(full_cmd), "sed \"%s\" \"%s\"", expression, path);
    }
    
    offset = snprintf(*result, buf_size, "Command: %s\n\n", full_cmd);
    
    char platform_cmd[2048];
    platform_prepare_command(full_cmd, platform_cmd, sizeof(platform_cmd));
    FILE* fp = popen(platform_cmd, "r");
    
    if (!fp) {
        free(*result);
        *result = strdup("Error: Failed to execute sed command");
        *result_len = strlen(*result);
        return -1;
    }
    
    char line[2048];
    while (fgets(line, sizeof(line), fp) && offset < (int)(buf_size - 2048)) {
        offset += snprintf(*result + offset, buf_size - offset, "%s", line);
    }
    
    int exit_code = pclose(fp);
    
    if (offset == (int)strlen(full_cmd) + 11) {
        offset += snprintf(*result + offset, buf_size - offset, "(no output)\n");
    }
    
    offset += snprintf(*result + offset, buf_size - offset, "\nExit code: %d", exit_code);
    
    *result_len = offset;
    return 0;
}

// Skill search tool - search local skills by query (empty query = list all)
int tool_skill_search(ToolArgs* args, char** result, int* result_len) {
    const char* query = tool_args_get(args, "query");
    if (!query) query = tool_args_get(args, "arg");
    
    // Get limit parameter (default 20)
    const char* limit_str = tool_args_get(args, "limit");
    int limit = 20;
    if (limit_str) {
        limit = atoi(limit_str);
        if (limit <= 0) limit = 20;
        if (limit > 100) limit = 100;
    }
    
    // If no query, or query is "all"/"*", list all skills
    if (!query || strlen(query) == 0 ||
        strcasecmp(query, "all") == 0 || strcmp(query, "*") == 0) {
        SkillRegistry* registry = skill_get_registry();
        if (!registry || registry->count == 0) {
            *result = strdup("No skills loaded");
            *result_len = strlen(*result);
            return 0;
        }
        
        size_t buf_size = 8192;
        *result = (char*)malloc(buf_size);
        if (!*result) {
            *result = strdup("Error: Memory allocation failed");
            *result_len = strlen(*result);
            return -1;
        }
        
        int offset = snprintf(*result, buf_size,
            "📋 All loaded skills (%d):\n"
            "─────────────────────────────────────────────────────────\n",
            registry->count);
        
        int count = 0;
        for (int i = 0; i < registry->count && count < limit && offset < (int)(buf_size - 256); i++) {
            Skill* skill = registry->skills[i];
            if (!skill) continue;
            
            offset += snprintf(*result + offset, buf_size - offset,
                "  %d. %s (%s/%s)\n"
                "     描述: %s\n"
                "     分类: %s | 作者: %s\n\n",
                count + 1,
                skill->name,
                skill_source_name(skill->source),
                skill_type_name(skill->type),
                skill->description ? skill->description : "无描述",
                skill->category ? skill->category : "General",
                skill->author ? skill->author : "Unknown");
            count++;
        }
        
        if (registry->count > limit) {
            offset += snprintf(*result + offset, buf_size - offset,
                "  ... 还有 %d 个技能未显示\n", registry->count - limit);
        }
        
        *result_len = offset;
        return 0;
    }
    
    // Search with query
    SkillMatchResult* matches = skill_search_local(query, limit);
    if (!matches || matches->count == 0) {
        char* no_result = (char*)malloc(128);
        if (no_result) {
            snprintf(no_result, 128, "No skills found matching '%s'", query);
        }
        *result = no_result ? no_result : strdup("No skills found");
        *result_len = strlen(*result);
        if (matches) skill_match_result_free(matches);
        return 0;
    }
    
    // Format result
    size_t buf_size = 4096;
    *result = (char*)malloc(buf_size);
    if (!*result) {
        skill_match_result_free(matches);
        *result = strdup("Error: Memory allocation failed");
        *result_len = strlen(*result);
        return -1;
    }
    
    int offset = snprintf(*result, buf_size,
        "🔍 Found %d skill(s) matching '%s':\n"
        "─────────────────────────────────────────────────────────\n",
        matches->count, query);
    
    for (int i = 0; i < matches->count && offset < (int)(buf_size - 256); i++) {
        Skill* skill = matches->matches[i].skill;
        int relevance = matches->matches[i].relevance;
        const char* matched_by = matches->matches[i].matched_by;
        
        offset += snprintf(*result + offset, buf_size - offset,
            "  [%d] %s (%s/%s)\n"
            "      匹配字段: %s\n"
            "      描述: %s\n\n",
            relevance,
            skill->name,
            skill_source_name(skill->source),
            skill_type_name(skill->type),
            matched_by,
            skill->description ? skill->description : "无描述");
    }
    
    skill_match_result_free(matches);
    *result_len = offset;
    return 0;
}

// Skill match tool - discover skills by keyword (for agent auto-discovery)
int tool_skill_match(ToolArgs* args, char** result, int* result_len) {
    const char* query = tool_args_get(args, "query");
    if (!query) query = tool_args_get(args, "keyword");
    if (!query) query = tool_args_get(args, "arg");
    
    if (!query || strlen(query) == 0) {
        *result = strdup("Error: No keyword provided for skill matching");
        *result_len = strlen(*result);
        return -1;
    }
    
    // Discover skills
    SkillMatchResult* matches = skill_discover(query);
    if (!matches || matches->count == 0) {
        char* no_result = (char*)malloc(128);
        if (no_result) {
            snprintf(no_result, 128, "No relevant skills discovered for '%s'", query);
        }
        *result = no_result ? no_result : strdup("No skills discovered");
        *result_len = strlen(*result);
        if (matches) skill_match_result_free(matches);
        return 0;
    }
    
    // Format result as JSON-like for agent consumption
    size_t buf_size = 8192;
    *result = (char*)malloc(buf_size);
    if (!*result) {
        skill_match_result_free(matches);
        *result = strdup("Error: Memory allocation failed");
        *result_len = strlen(*result);
        return -1;
    }
    
    int offset = snprintf(*result, buf_size,
        "{\n"
        "  \"query\": \"%s\",\n"
        "  \"count\": %d,\n"
        "  \"matches\": [\n",
        query, matches->count);
    
    for (int i = 0; i < matches->count && offset < (int)(buf_size - 512); i++) {
        Skill* skill = matches->matches[i].skill;
        int relevance = matches->matches[i].relevance;
        const char* matched_by = matches->matches[i].matched_by;
        
        offset += snprintf(*result + offset, buf_size - offset,
            "    {\n"
            "      \"name\": \"%s\",\n"
            "      \"relevance\": %d,\n"
            "      \"matched_by\": \"%s\",\n"
            "      \"source\": \"%s\",\n"
            "      \"type\": \"%s\",\n"
            "      \"description\": \"%s\",\n"
            "      \"category\": \"%s\"\n"
            "    }%s\n",
            skill->name,
            relevance,
            matched_by,
            skill_source_name(skill->source),
            skill_type_name(skill->type),
            skill->description ? skill->description : "",
            skill->category ? skill->category : "General",
            (i < matches->count - 1) ? "," : "");
    }
    
    offset += snprintf(*result + offset, buf_size - offset, "  ]\n}");
    
    skill_match_result_free(matches);
    *result_len = offset;
    return 0;
}

// Skill preview tool - preview skill content
int tool_skill_preview(ToolArgs* args, char** result, int* result_len) {
    const char* name = tool_args_get(args, "name");
    if (!name) name = tool_args_get(args, "skill");
    if (!name) name = tool_args_get(args, "arg");
    
    if (!name || strlen(name) == 0) {
        *result = strdup("Error: No skill name provided");
        *result_len = strlen(*result);
        return -1;
    }
    
    // Get max_lines parameter (default 5)
    const char* lines_str = tool_args_get(args, "lines");
    int max_lines = 5;
    if (lines_str) {
        max_lines = atoi(lines_str);
        if (max_lines <= 0) max_lines = 5;
        if (max_lines > 50) max_lines = 50;
    }
    
    // Get preview
    char* preview = skill_preview(name, max_lines);
    if (!preview) {
        char* err = (char*)malloc(128);
        if (err) {
            snprintf(err, 128, "Error: Skill '%s' not found", name);
        }
        *result = err ? err : strdup("Error: Skill not found");
        *result_len = strlen(*result);
        return -1;
    }
    
    *result = preview;
    *result_len = strlen(preview);
    return 0;
}
