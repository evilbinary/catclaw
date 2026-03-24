#include "skill.h"
#include "common/plugin.h"
#include "common/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

// Skill directories (loaded in order: low priority first)
#define SKILL_DIR_BUILTIN   NULL  // Built-in skills are registered programmatically
#define SKILL_DIR_PLUGIN    "skills"
#define SKILL_DIR_LOCAL     "local_skills"
#define MAX_PATH_LEN        512

// Global skill registry
static SkillRegistry g_skill_registry = {
    .skills = NULL,
    .count = 0,
    .capacity = 0
};

// Forward declaration
static bool skill_add_to_registry(Skill *skill);

// Get skill source name string
static const char *skill_source_name(SkillSource source) {
    switch (source) {
        case SKILL_SOURCE_BUILTIN:   return "builtin";
        case SKILL_SOURCE_PLUGIN:    return "plugin";
        case SKILL_SOURCE_LOCAL:     return "local";
        case SKILL_SOURCE_WORKSPACE: return "workspace";
        default: return "unknown";
    }
}

// Get workspace skills directory path
static char *get_workspace_skills_path(void) {
    char *home = getenv("HOME");
    if (!home) {
        return NULL;
    }
    
    char *path = malloc(MAX_PATH_LEN);
    if (!path) {
        return NULL;
    }
    
    snprintf(path, MAX_PATH_LEN, "%s/.catclaw/workspace/skills", home);
    return path;
}

// Auto-load skills from directory with specified source type
static int skill_auto_load_from_dir(const char *dir_path, SkillSource source) {
    if (!dir_path) {
        return 0;
    }
    
    DIR *dir = opendir(dir_path);
    if (!dir) {
        return 0;  // Directory doesn't exist, not an error
    }
    
    int loaded = 0;
    struct dirent *entry;
    
    while ((entry = readdir(dir)) != NULL) {
        // Skip hidden files and directories
        if (entry->d_name[0] == '.') {
            continue;
        }
        
        // Check if it's a .so file
        const char *ext = strrchr(entry->d_name, '.');
        if (!ext || strcmp(ext, ".so") != 0) {
            continue;
        }
        
        // Build full path
        char full_path[MAX_PATH_LEN];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        
        // Check if file exists and is readable
        struct stat st;
        if (stat(full_path, &st) != 0 || !S_ISREG(st.st_mode)) {
            continue;
        }
        
        // Try to load the skill
        log_info("[Skill] Auto-loading %s skill: %s", skill_source_name(source), full_path);
        if (skill_load_from_source(full_path, source)) {
            loaded++;
        }
    }
    
    closedir(dir);
    return loaded;
}

// Initialize skill system
bool skill_system_init(void) {
    g_skill_registry.skills = malloc(sizeof(Skill *) * 20);
    if (!g_skill_registry.skills) {
        fprintf(stderr, "Failed to allocate memory for skill registry\n");
        return false;
    }
    g_skill_registry.capacity = 20;
    g_skill_registry.count = 0;
    
    int total_loaded = 0;
    
    // Load skills in priority order (low to high, so higher priority overrides)
    // 1. Built-in skills (registered programmatically, skip directory scan)
    
    // 2. Plugin skills (from skills/ directory)
    int plugin_skills = skill_auto_load_from_dir(SKILL_DIR_PLUGIN, SKILL_SOURCE_PLUGIN);
    if (plugin_skills > 0) {
        log_info("[Skill] Loaded %d plugin skill(s) from %s", plugin_skills, SKILL_DIR_PLUGIN);
        total_loaded += plugin_skills;
    }
    
    // 3. Local skills (from ./local_skills/)
    int local_skills = skill_auto_load_from_dir(SKILL_DIR_LOCAL, SKILL_SOURCE_LOCAL);
    if (local_skills > 0) {
        log_info("[Skill] Loaded %d local skill(s) from %s", local_skills, SKILL_DIR_LOCAL);
        total_loaded += local_skills;
    }
    
    // 4. Workspace skills (from ~/.catclaw/workspace/skills/)
    char *workspace_path = get_workspace_skills_path();
    if (workspace_path) {
        int workspace_skills = skill_auto_load_from_dir(workspace_path, SKILL_SOURCE_WORKSPACE);
        if (workspace_skills > 0) {
            log_info("[Skill] Loaded %d workspace skill(s) from %s", workspace_skills, workspace_path);
            total_loaded += workspace_skills;
        }
        free(workspace_path);
    }
    
    log_info("[Skill] System initialized with %d skill(s)", total_loaded);
    return true;
}

// Cleanup skill system
void skill_system_cleanup(void) {
    for (int i = 0; i < g_skill_registry.count; i++) {
        Skill *skill = g_skill_registry.skills[i];
        if (skill) {
            free(skill->name);
            free(skill->description);
            free(skill->version);
            free(skill->author);
            free(skill->category);
            free(skill->path);
            free(skill);
        }
    }
    free(g_skill_registry.skills);
    g_skill_registry.skills = NULL;
    g_skill_registry.count = 0;
    g_skill_registry.capacity = 0;
}

// Add skill to registry (handles override by source priority)
static bool skill_add_to_registry(Skill *new_skill) {
    // Check if skill with same name exists
    for (int i = 0; i < g_skill_registry.count; i++) {
        Skill *existing = g_skill_registry.skills[i];
        if (existing && strcmp(existing->name, new_skill->name) == 0) {
            // Compare source priority
            if (new_skill->source >= existing->source) {
                // New skill has higher or equal priority, replace
                log_info("[Skill] Replacing '%s' (%s -> %s)", 
                         existing->name,
                         skill_source_name(existing->source),
                         skill_source_name(new_skill->source));
                
                // Free existing skill
                free(existing->name);
                free(existing->description);
                free(existing->version);
                free(existing->author);
                free(existing->category);
                free(existing->path);
                free(existing);
                
                // Replace with new skill
                g_skill_registry.skills[i] = new_skill;
                return true;
            } else {
                // Existing skill has higher priority, skip new skill
                log_info("[Skill] Keeping existing '%s' (%s, new skill '%s' ignored)",
                         existing->name,
                         skill_source_name(existing->source),
                         skill_source_name(new_skill->source));
                
                // Free new skill
                free(new_skill->name);
                free(new_skill->description);
                free(new_skill->version);
                free(new_skill->author);
                free(new_skill->category);
                free(new_skill->path);
                free(new_skill);
                
                return false;  // Not added, but not an error
            }
        }
    }
    
    // No existing skill with same name, add new one
    if (g_skill_registry.count >= g_skill_registry.capacity) {
        int new_capacity = g_skill_registry.capacity * 2;
        Skill **new_skills = realloc(g_skill_registry.skills, sizeof(Skill *) * new_capacity);
        if (!new_skills) {
            fprintf(stderr, "Failed to reallocate memory for skill registry\n");
            return false;
        }
        g_skill_registry.skills = new_skills;
        g_skill_registry.capacity = new_capacity;
    }
    
    g_skill_registry.skills[g_skill_registry.count] = new_skill;
    g_skill_registry.count++;
    return true;
}

// Load a skill from a plugin (default source: PLUGIN)
bool skill_load(const char *path) {
    return skill_load_from_source(path, SKILL_SOURCE_PLUGIN);
}

// Unload a plugin by extracting name from path
static void unload_plugin_by_path(const char *path) {
    const char *plugin_name = strrchr(path, '/');
    if (!plugin_name) {
        plugin_name = strrchr(path, '\\');
    }
    if (plugin_name) {
        plugin_name++;
    } else {
        plugin_name = path;
    }
    
    // Remove extension
    char *name_copy = strdup(plugin_name);
    char *dot = strrchr(name_copy, '.');
    if (dot) {
        *dot = '\0';
    }
    
    plugin_unload(name_copy);
    free(name_copy);
}

// Load a skill from a plugin with specified source
bool skill_load_from_source(const char *path, SkillSource source) {
    // Get the plugin name from the path first
    const char *plugin_name = strrchr(path, '/');
    if (!plugin_name) {
        plugin_name = strrchr(path, '\\');
    }
    if (plugin_name) {
        plugin_name++;
    } else {
        plugin_name = path;
    }
    
    // Make a copy for later use
    char *name_copy = strdup(plugin_name);
    char *dot = strrchr(name_copy, '.');
    if (dot) {
        *dot = '\0';
    }
    
    // Check if plugin already loaded (same filename), if so, check priority
    Plugin *existing_plugin = plugin_find(name_copy);
    if (existing_plugin) {
        // Plugin file already loaded, check if we need to replace
        // Find the skill using this plugin
        for (int i = 0; i < g_skill_registry.count; i++) {
            Skill *existing = g_skill_registry.skills[i];
            if (existing && existing->source < source) {
                // Lower priority skill exists, unload it first
                log_info("[Skill] Unloading lower priority skill: %s", existing->name);
                
                // Free skill data
                free(existing->name);
                free(existing->description);
                free(existing->version);
                free(existing->author);
                free(existing->category);
                free(existing->path);
                free(existing);
                
                // Shift remaining skills
                for (int j = i; j < g_skill_registry.count - 1; j++) {
                    g_skill_registry.skills[j] = g_skill_registry.skills[j + 1];
                }
                g_skill_registry.count--;
                break;
            }
        }
        
        // Unload the old plugin
        plugin_unload(name_copy);
        existing_plugin = NULL;
    }
    
    // Load the plugin
    if (!plugin_load(path)) {
        fprintf(stderr, "Failed to load plugin: %s\n", path);
        free(name_copy);
        return false;
    }
    
    // Find the plugin
    Plugin *plugin = plugin_find(name_copy);
    free(name_copy);
    
    if (!plugin || plugin->type != PLUGIN_TYPE_SKILL) {
        fprintf(stderr, "Plugin is not a skill: %s\n", plugin_name);
        return false;
    }
    
    // Get skill information from the plugin
    char *(*get_skill_name)(void) = (char *(*)(void))plugin->get_function("skill_get_name");
    char *(*get_skill_description)(void) = (char *(*)(void))plugin->get_function("skill_get_description");
    char *(*get_skill_version)(void) = (char *(*)(void))plugin->get_function("skill_get_version");
    char *(*get_skill_author)(void) = (char *(*)(void))plugin->get_function("skill_get_author");
    char *(*get_skill_category)(void) = (char *(*)(void))plugin->get_function("skill_get_category");
    char *(*skill_execute_func)(const char *) = (char *(*)(const char *))plugin->get_function("skill_execute");
    
    if (!get_skill_name || !skill_execute_func) {
        fprintf(stderr, "Invalid skill plugin: missing required functions\n");
        return false;
    }
    
    // Create skill structure
    Skill *skill = malloc(sizeof(Skill));
    if (!skill) {
        fprintf(stderr, "Failed to allocate memory for skill\n");
        return false;
    }
    
    skill->name = strdup(get_skill_name());
    skill->description = get_skill_description() ? strdup(get_skill_description()) : strdup("");
    skill->version = get_skill_version() ? strdup(get_skill_version()) : strdup("1.0");
    skill->author = get_skill_author() ? strdup(get_skill_author()) : strdup("Unknown");
    skill->category = get_skill_category() ? strdup(get_skill_category()) : strdup("General");
    skill->execute = skill_execute_func;
    skill->enabled = true;
    skill->source = source;
    skill->path = strdup(path);
    
    if (skill_add_to_registry(skill)) {
        log_info("[Skill] Loaded: %s [%s]", skill->name, skill_source_name(source));
        return true;
    }
    
    return false;
}

// Register a built-in skill
bool skill_register_builtin(const char *name, const char *description,
                            const char *version, char *(*execute)(const char *)) {
    if (!name || !execute) {
        return false;
    }
    
    Skill *skill = malloc(sizeof(Skill));
    if (!skill) {
        return false;
    }
    
    skill->name = strdup(name);
    skill->description = description ? strdup(description) : strdup("");
    skill->version = version ? strdup(version) : strdup("1.0");
    skill->author = strdup("CatClaw");
    skill->category = strdup("Built-in");
    skill->execute = execute;
    skill->enabled = true;
    skill->source = SKILL_SOURCE_BUILTIN;
    skill->path = NULL;
    
    if (skill_add_to_registry(skill)) {
        log_info("[Skill] Registered built-in skill: %s", name);
        return true;
    }
    
    return false;
}

// Unload a skill
bool skill_unload(const char *name) {
    for (int i = 0; i < g_skill_registry.count; i++) {
        Skill *skill = g_skill_registry.skills[i];
        if (skill && strcmp(skill->name, name) == 0) {
            // For plugin-based skills, unload the plugin
            if (skill->source != SKILL_SOURCE_BUILTIN && skill->path) {
                // Extract plugin name from path for plugin_unload
                const char *plugin_name = strrchr(skill->path, '/');
                if (plugin_name) {
                    plugin_name++;
                } else {
                    plugin_name = skill->path;
                }
                
                // Remove extension
                char *name_copy = strdup(plugin_name);
                char *dot = strrchr(name_copy, '.');
                if (dot) {
                    *dot = '\0';
                }
                
                plugin_unload(name_copy);
                free(name_copy);
            }
            
            // Remove from registry
            free(skill->name);
            free(skill->description);
            free(skill->version);
            free(skill->author);
            free(skill->category);
            free(skill->path);
            free(skill);
            
            // Shift remaining skills
            for (int j = i; j < g_skill_registry.count - 1; j++) {
                g_skill_registry.skills[j] = g_skill_registry.skills[j + 1];
            }
            g_skill_registry.count--;
            
            log_info("[Skill] Unloaded: %s", name);
            return true;
        }
    }
    
    fprintf(stderr, "Skill not found: %s\n", name);
    return false;
}

// Find a skill by name (returns highest priority if duplicates exist)
Skill *skill_find(const char *name) {
    Skill *found = NULL;
    
    for (int i = 0; i < g_skill_registry.count; i++) {
        Skill *skill = g_skill_registry.skills[i];
        if (skill && strcmp(skill->name, name) == 0) {
            // Return first match (registry maintains priority order after add_to_registry)
            if (!found || skill->source > found->source) {
                found = skill;
            }
        }
    }
    
    return found;
}

// Execute a skill
char *skill_execute_skill(const char *name, const char *params) {
    Skill *skill = skill_find(name);
    if (!skill) {
        fprintf(stderr, "Skill not found: %s\n", name);
        return strdup("Error: Skill not found");
    }
    
    if (!skill->enabled) {
        fprintf(stderr, "Skill is disabled: %s\n", name);
        return strdup("Error: Skill is disabled");
    }
    
    if (!skill->execute) {
        fprintf(stderr, "Skill has no execute function: %s\n", name);
        return strdup("Error: Skill has no execute function");
    }
    
    return skill->execute(params);
}

// List all skills
void skill_list(void) {
    if (g_skill_registry.count == 0) {
        printf("No skills loaded\n");
        return;
    }
    
    printf("Loaded skills:\n");
    for (int i = 0; i < g_skill_registry.count; i++) {
        Skill *skill = g_skill_registry.skills[i];
        if (skill) {
            printf("  %s (%s) [%s] - %s\n", 
                   skill->name, skill->version, 
                   skill_source_name(skill->source), 
                   skill->description);
            printf("    Author: %s | Category: %s | %s\n", 
                   skill->author, skill->category,
                   skill->enabled ? "Enabled" : "Disabled");
        }
    }
}

// Get the skill registry
SkillRegistry *skill_get_registry(void) {
    return &g_skill_registry;
}

// Enable a skill
bool skill_enable(const char *name) {
    Skill *skill = skill_find(name);
    if (!skill) {
        fprintf(stderr, "Skill not found: %s\n", name);
        return false;
    }
    
    skill->enabled = true;
    printf("Skill enabled: %s\n", name);
    return true;
}

// Disable a skill
bool skill_disable(const char *name) {
    Skill *skill = skill_find(name);
    if (!skill) {
        fprintf(stderr, "Skill not found: %s\n", name);
        return false;
    }
    
    skill->enabled = false;
    printf("Skill disabled: %s\n", name);
    return true;
}
