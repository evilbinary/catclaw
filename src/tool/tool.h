#ifndef TOOL_H
#define TOOL_H

#include <stdbool.h>

// Tool structure
typedef struct {
    char* name;
    char* description;
    char* parameters_schema;  // JSON Schema
    int (*execute)(const char* args, char** result, int* result_len);
} Tool;

// Tool registry structure
typedef struct {
    Tool** tools;
    int count;
    int capacity;
} ToolRegistry;

// Functions
Tool* tool_create(const char* name, const char* description, const char* parameters_schema, int (*execute)(const char* args, char** result, int* result_len));
void tool_destroy(Tool* tool);

ToolRegistry* tool_registry_init(void);
void tool_registry_destroy(ToolRegistry* registry);
bool tool_registry_register(ToolRegistry* registry, Tool* tool);
Tool* tool_registry_get(ToolRegistry* registry, const char* name);
void tool_registry_list(ToolRegistry* registry);

// Built-in tools
int tool_calculate(const char* args, char** result, int* result_len);
int tool_get_time(const char* args, char** result, int* result_len);
int tool_reverse_string(const char* args, char** result, int* result_len);
int tool_read_file(const char* args, char** result, int* result_len);
int tool_write_file(const char* args, char** result, int* result_len);
int tool_search_web(const char* args, char** result, int* result_len);
int tool_save_memory(const char* args, char** result, int* result_len);
int tool_read_memory(const char* args, char** result, int* result_len);
int tool_get_weather(const char* args, char** result, int* result_len);
int tool_list_directory(const char* args, char** result, int* result_len);

#endif // TOOL_H
