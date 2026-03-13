#ifndef PLUGIN_H
#define PLUGIN_H

#include <stdbool.h>

// Plugin types
typedef enum {
    PLUGIN_TYPE_CHANNEL,
    PLUGIN_TYPE_TOOL,
    PLUGIN_TYPE_SKILL,
    PLUGIN_TYPE_OTHER,
    PLUGIN_TYPE_MAX
} PluginType;

// Plugin structure
typedef struct {
    char *name;
    char *version;
    char *description;
    PluginType type;
    void *handle;
    bool loaded;
    
    // Plugin lifecycle functions
    bool (*init)(void);
    void (*cleanup)(void);
    
    // Plugin specific functions
    void *(*get_function)(const char *name);
} Plugin;

// Plugin registry structure
typedef struct {
    Plugin **plugins;
    int count;
    int capacity;
} PluginRegistry;

// Functions
bool plugin_system_init(void);
void plugin_system_cleanup(void);
bool plugin_load(const char *path);
bool plugin_unload(const char *name);
Plugin *plugin_find(const char *name);
void plugin_list(void);
PluginRegistry *plugin_get_registry(void);

#endif // PLUGIN_H
