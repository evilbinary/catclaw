#ifndef TOOL_H
#define TOOL_H

#include <stdbool.h>

// Tool argument structure (key-value pair)
typedef struct {
    char* key;
    char* value;
} ToolArg;

// Tool arguments list
typedef struct {
    ToolArg* args;
    int count;
} ToolArgs;

// Tool structure
typedef struct {
    char* name;
    char* description;
    char* parameters_schema;  // JSON Schema
    int (*execute)(ToolArgs* args, char** result, int* result_len);
} Tool;

// Tool registry structure
typedef struct {
    Tool** tools;
    int count;
    int capacity;
} ToolRegistry;

// Functions
Tool* tool_create(const char* name, const char* description, const char* parameters_schema, int (*execute)(ToolArgs* args, char** result, int* result_len));
void tool_destroy(Tool* tool);

ToolRegistry* tool_registry_init(void);
void tool_registry_destroy(ToolRegistry* registry);
bool tool_registry_register(ToolRegistry* registry, Tool* tool);
Tool* tool_registry_get(ToolRegistry* registry, const char* name);
void tool_registry_list(ToolRegistry* registry);

// Helper to get argument value by key
const char* tool_args_get(ToolArgs* args, const char* key);

// Helper to create ToolArgs from a single string argument
ToolArgs* tool_args_from_string(const char* value);

// Helper to free ToolArgs
void tool_args_free(ToolArgs* args);

// Built-in tools
int tool_calculate(ToolArgs* args, char** result, int* result_len);
int tool_get_time(ToolArgs* args, char** result, int* result_len);
int tool_reverse_string(ToolArgs* args, char** result, int* result_len);
int tool_read_file(ToolArgs* args, char** result, int* result_len);
int tool_write_file(ToolArgs* args, char** result, int* result_len);
int tool_search_web(ToolArgs* args, char** result, int* result_len);
int tool_save_memory(ToolArgs* args, char** result, int* result_len);
int tool_read_memory(ToolArgs* args, char** result, int* result_len);
int tool_delete_memory(ToolArgs* args, char** result, int* result_len);
int tool_get_weather(ToolArgs* args, char** result, int* result_len);
int tool_list_directory(ToolArgs* args, char** result, int* result_len);
int tool_web_fetch(ToolArgs* args, char** result, int* result_len);
int tool_shell_execute(ToolArgs* args, char** result, int* result_len);
int tool_grep_execute(ToolArgs* args, char** result, int* result_len);
int tool_sed_execute(ToolArgs* args, char** result, int* result_len);

// Skill discovery tools
int tool_skill_search(ToolArgs* args, char** result, int* result_len);
int tool_skill_match(ToolArgs* args, char** result, int* result_len);
int tool_skill_preview(ToolArgs* args, char** result, int* result_len);

// History recall tool
int tool_recall_history(ToolArgs* args, char** result, int* result_len);

#endif // TOOL_H
