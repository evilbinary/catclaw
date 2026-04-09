#include "plugin.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Plugin file extension
#define PLUGIN_EXT platform_get_plugin_ext()

// Plugin registry
static PluginRegistry *registry = NULL;

// Initialize plugin system
bool plugin_system_init(void) {
    registry = (PluginRegistry *)malloc(sizeof(PluginRegistry));
    if (!registry) {
        fprintf(stderr, "Memory allocation failed\n");
        return false;
    }
    
    registry->plugins = NULL;
    registry->count = 0;
    registry->capacity = 10;
    
    registry->plugins = (Plugin **)malloc(sizeof(Plugin *) * registry->capacity);
    if (!registry->plugins) {
        fprintf(stderr, "Memory allocation failed\n");
        free(registry);
        return false;
    }
    
    printf("Plugin system initialized\n");
    return true;
}

// Cleanup plugin system
void plugin_system_cleanup(void) {
    if (registry) {
        // Unload all plugins
        for (int i = 0; i < registry->count; i++) {
            Plugin *plugin = registry->plugins[i];
            if (plugin->loaded) {
                if (plugin->cleanup) {
                    plugin->cleanup();
                }
                dlclose(plugin->handle);
            }
            free(plugin->name);
            free(plugin->version);
            free(plugin->description);
            free(plugin);
        }
        
        free(registry->plugins);
        free(registry);
        registry = NULL;
    }
    
    printf("Plugin system cleaned up\n");
}

// Load a plugin
bool plugin_load(const char *path) {
    if (!registry) {
        fprintf(stderr, "Plugin system not initialized\n");
        return false;
    }
    
    // Make a copy of path to avoid modifying the original
    char *path_copy = strdup(path);
    if (!path_copy) {
        fprintf(stderr, "Memory allocation failed\n");
        return false;
    }
    
    // Extract plugin name from path
    char *plugin_name = strrchr(path_copy, '/');
    if (!plugin_name) {
        plugin_name = strrchr(path_copy, '\\');
    }
    if (!plugin_name) {
        plugin_name = path_copy;
    } else {
        plugin_name++;
    }
    
    // Create a copy of plugin name for registration (without extension)
    char *name_copy = strdup(plugin_name);
    if (!name_copy) {
        fprintf(stderr, "Memory allocation failed\n");
        free(path_copy);
        return false;
    }
    
    // Remove extension from name_copy
    char *ext = strstr(name_copy, PLUGIN_EXT);
    if (ext) {
        *ext = '\0';
    }
    
    if (plugin_find(name_copy)) {
        fprintf(stderr, "Plugin %s already loaded\n", name_copy);
        free(path_copy);
        free(name_copy);
        return false;
    }
    
    // Load plugin library using original path
    void *handle = platform_load_library(path);
    if (!handle) {
        fprintf(stderr, "Failed to load plugin %s: %s\n", path, platform_dlerror());
        free(path_copy);
        free(name_copy);
        return false;
    }
    
    // Allocate plugin structure
    Plugin *plugin = (Plugin *)malloc(sizeof(Plugin));
    if (!plugin) {
        fprintf(stderr, "Memory allocation failed\n");
        platform_unload_library(handle);
        free(path_copy);
        free(name_copy);
        return false;
    }
    
    // Initialize plugin structure
    plugin->name = name_copy;  // Take ownership of name_copy
    plugin->version = strdup("1.0.0"); // Default version
    plugin->description = strdup("Unknown plugin"); // Default description
    plugin->type = PLUGIN_TYPE_OTHER;
    plugin->handle = handle;
    plugin->loaded = false;
    
    // Get plugin initialization function
    plugin->init = (bool (*)(void))platform_get_function(handle, "plugin_init");
    plugin->cleanup = (void (*)(void))platform_get_function(handle, "plugin_cleanup");
    plugin->get_function = (void *(*)(const char *))platform_get_function(handle, "plugin_get_function");
    
    // Detect plugin type by checking available functions
    if (plugin->get_function) {
        // Check if it's a skill plugin
        void *skill_exec = plugin->get_function("skill_execute");
        if (skill_exec) {
            plugin->type = PLUGIN_TYPE_SKILL;
            
            // Get skill metadata for description and version only
            char *(*get_desc)(void) = (char *(*)(void))plugin->get_function("skill_get_description");
            char *(*get_ver)(void) = (char *(*)(void))plugin->get_function("skill_get_version");
            
            if (get_desc) {
                free(plugin->description);
                plugin->description = strdup(get_desc());
            }
            if (get_ver) {
                free(plugin->version);
                plugin->version = strdup(get_ver());
            }
        }
    }
    
    // Call initialization function if available
    if (plugin->init) {
        if (!plugin->init()) {
            fprintf(stderr, "Failed to initialize plugin %s\n", name_copy);
            free(plugin->name);
            free(plugin->version);
            free(plugin->description);
            free(plugin);
            dlclose(handle);
            free(path_copy);
            return false;
        }
    }
    
    plugin->loaded = true;
    
    // Add to registry
    if (registry->count >= registry->capacity) {
        registry->capacity *= 2;
        Plugin **new_plugins = (Plugin **)realloc(registry->plugins, sizeof(Plugin *) * registry->capacity);
        if (!new_plugins) {
            fprintf(stderr, "Memory allocation failed\n");
            free(plugin->name);
            free(plugin->version);
            free(plugin->description);
            free(plugin);
            dlclose(handle);
            free(path_copy);
            return false;
        }
        registry->plugins = new_plugins;
    }
    
    registry->plugins[registry->count++] = plugin;
    
    free(path_copy);  // Free path copy, name_copy is owned by plugin
    printf("Plugin %s loaded successfully\n", name_copy);
    return true;
}

// Unload a plugin
bool plugin_unload(const char *name) {
    if (!registry) {
        fprintf(stderr, "Plugin system not initialized\n");
        return false;
    }
    
    for (int i = 0; i < registry->count; i++) {
        Plugin *plugin = registry->plugins[i];
        if (strcmp(plugin->name, name) == 0) {
            // Call cleanup function if available
            if (plugin->cleanup) {
                plugin->cleanup();
            }
            
            // Close plugin handle
            dlclose(plugin->handle);
            
            // Remove from registry
            free(plugin->name);
            free(plugin->version);
            free(plugin->description);
            free(plugin);
            
            // Shift remaining plugins
            for (int j = i; j < registry->count - 1; j++) {
                registry->plugins[j] = registry->plugins[j + 1];
            }
            registry->count--;
            
            printf("Plugin %s unloaded successfully\n", name);
            return true;
        }
    }
    
    fprintf(stderr, "Plugin %s not found\n", name);
    return false;
}

// Find a plugin by name
Plugin *plugin_find(const char *name) {
    if (!registry) {
        return NULL;
    }
    
    for (int i = 0; i < registry->count; i++) {
        Plugin *plugin = registry->plugins[i];
        if (strcmp(plugin->name, name) == 0) {
            return plugin;
        }
    }
    
    return NULL;
}

// List all loaded plugins
void plugin_list(void) {
    if (!registry) {
        printf("Plugin system not initialized\n");
        return;
    }
    
    if (registry->count == 0) {
        printf("No plugins loaded\n");
        return;
    }
    
    printf("Loaded plugins:\n");
    for (int i = 0; i < registry->count; i++) {
        Plugin *plugin = registry->plugins[i];
        printf("  %s v%s (%s) - %s\n", 
               plugin->name, 
               plugin->version, 
               plugin->type == PLUGIN_TYPE_CHANNEL ? "Channel" : 
               plugin->type == PLUGIN_TYPE_TOOL ? "Tool" : 
               plugin->type == PLUGIN_TYPE_SKILL ? "Skill" : "Other",
               plugin->description);
    }
}

// Get plugin list as string
char* plugin_list_to_string(void) {
    if (!registry) {
        return strdup("Plugin system not initialized");
    }
    
    if (registry->count == 0) {
        return strdup("No plugins loaded");
    }
    
    // Calculate approximate size needed
    size_t size = 1024; // Initial size
    char* buf = (char*)malloc(size);
    if (!buf) return NULL;
    
    int pos = snprintf(buf, size, "Loaded plugins:\n");
    
    for (int i = 0; i < registry->count; i++) {
        Plugin *plugin = registry->plugins[i];
        if (plugin) {
            // Check if we need more space
            if (pos + 512 > size) {
                size *= 2;
                char* new_buf = (char*)realloc(buf, size);
                if (!new_buf) {
                    free(buf);
                    return NULL;
                }
                buf = new_buf;
            }
            
            pos += snprintf(buf + pos, size - pos, "  %s v%s (%s) - %s\n", 
                           plugin->name, 
                           plugin->version, 
                           plugin->type == PLUGIN_TYPE_CHANNEL ? "Channel" : 
                           plugin->type == PLUGIN_TYPE_TOOL ? "Tool" : 
                           plugin->type == PLUGIN_TYPE_SKILL ? "Skill" : "Other",
                           plugin->description);
        }
    }
    
    return buf;
}

// Get plugin registry
PluginRegistry *plugin_get_registry(void) {
    return registry;
}
