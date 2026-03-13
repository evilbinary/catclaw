#include "plugin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Platform-specific includes for dynamic loading
#ifdef _WIN32
#include <windows.h>
#define PLUGIN_EXT ".dll"
#define dlsym GetProcAddress
#define dlclose FreeLibrary
#define dlerror GetLastError
#else
#include <dlfcn.h>
#define PLUGIN_EXT ".so"
#define RTLD_LAZY 1
#endif

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
    
    // Check if plugin already loaded
    char *plugin_name = strrchr(path, '\\');
    if (!plugin_name) {
        plugin_name = (char *)path;
    } else {
        plugin_name++;
    }
    
    // Remove extension
    char *ext = strstr(plugin_name, PLUGIN_EXT);
    if (ext) {
        *ext = '\0';
    }
    
    if (plugin_find(plugin_name)) {
        fprintf(stderr, "Plugin %s already loaded\n", plugin_name);
        return false;
    }
    
    // Load plugin library
    void *handle;
#ifdef _WIN32
    handle = LoadLibraryA(path);
#else
    handle = dlopen(path, RTLD_LAZY);
#endif
    if (!handle) {
        fprintf(stderr, "Failed to load plugin %s: %s\n", path, dlerror());
        return false;
    }
    
    // Allocate plugin structure
    Plugin *plugin = (Plugin *)malloc(sizeof(Plugin));
    if (!plugin) {
        fprintf(stderr, "Memory allocation failed\n");
        dlclose(handle);
        return false;
    }
    
    // Initialize plugin structure
    plugin->name = strdup(plugin_name);
    plugin->version = strdup("1.0.0"); // Default version
    plugin->description = strdup("Unknown plugin"); // Default description
    plugin->type = PLUGIN_TYPE_OTHER;
    plugin->handle = handle;
    plugin->loaded = false;
    
    // Get plugin initialization function
    plugin->init = (bool (*)(void))dlsym(handle, "plugin_init");
    plugin->cleanup = (void (*)(void))dlsym(handle, "plugin_cleanup");
    plugin->get_function = (void *(*)(const char *))dlsym(handle, "plugin_get_function");
    
    // Call initialization function if available
    if (plugin->init) {
        if (!plugin->init()) {
            fprintf(stderr, "Failed to initialize plugin %s\n", plugin_name);
            free(plugin->name);
            free(plugin->version);
            free(plugin->description);
            free(plugin);
            dlclose(handle);
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
            return false;
        }
        registry->plugins = new_plugins;
    }
    
    registry->plugins[registry->count++] = plugin;
    
    printf("Plugin %s loaded successfully\n", plugin_name);
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

// Get plugin registry
PluginRegistry *plugin_get_registry(void) {
    return registry;
}
