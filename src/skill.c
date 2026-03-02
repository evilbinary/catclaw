#include "skill.h"
#include "plugin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global skill registry
static SkillRegistry g_skill_registry = {
    .skills = NULL,
    .count = 0,
    .capacity = 0
};

// Initialize skill system
bool skill_system_init(void) {
    g_skill_registry.skills = malloc(sizeof(Skill *) * 10);
    if (!g_skill_registry.skills) {
        fprintf(stderr, "Failed to allocate memory for skill registry\n");
        return false;
    }
    g_skill_registry.capacity = 10;
    g_skill_registry.count = 0;
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
            free(skill);
        }
    }
    free(g_skill_registry.skills);
    g_skill_registry.skills = NULL;
    g_skill_registry.count = 0;
    g_skill_registry.capacity = 0;
}

// Load a skill from a plugin
bool skill_load(const char *path) {
    // Load the plugin
    if (!plugin_load(path)) {
        fprintf(stderr, "Failed to load plugin: %s\n", path);
        return false;
    }
    
    // Get the plugin name from the path
    const char *plugin_name = strrchr(path, '/');
    if (!plugin_name) {
        plugin_name = strrchr(path, '\\');
    }
    if (plugin_name) {
        plugin_name++;
    } else {
        plugin_name = path;
    }
    
    // Remove file extension
    char *name_copy = strdup(plugin_name);
    char *dot = strrchr(name_copy, '.');
    if (dot) {
        *dot = '\0';
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
    
    // Add to registry
    if (g_skill_registry.count >= g_skill_registry.capacity) {
        int new_capacity = g_skill_registry.capacity * 2;
        Skill **new_skills = realloc(g_skill_registry.skills, sizeof(Skill *) * new_capacity);
        if (!new_skills) {
            fprintf(stderr, "Failed to reallocate memory for skill registry\n");
            free(skill->name);
            free(skill->description);
            free(skill->version);
            free(skill->author);
            free(skill->category);
            free(skill);
            return false;
        }
        g_skill_registry.skills = new_skills;
        g_skill_registry.capacity = new_capacity;
    }
    
    g_skill_registry.skills[g_skill_registry.count] = skill;
    g_skill_registry.count++;
    
    printf("Skill loaded: %s\n", skill->name);
    return true;
}

// Unload a skill
bool skill_unload(const char *name) {
    for (int i = 0; i < g_skill_registry.count; i++) {
        Skill *skill = g_skill_registry.skills[i];
        if (skill && strcmp(skill->name, name) == 0) {
            // Unload the plugin
            if (!plugin_unload(name)) {
                fprintf(stderr, "Failed to unload plugin for skill: %s\n", name);
                return false;
            }
            
            // Remove from registry
            free(skill->name);
            free(skill->description);
            free(skill->version);
            free(skill->author);
            free(skill->category);
            free(skill);
            
            // Shift remaining skills
            for (int j = i; j < g_skill_registry.count - 1; j++) {
                g_skill_registry.skills[j] = g_skill_registry.skills[j + 1];
            }
            g_skill_registry.count--;
            
            printf("Skill unloaded: %s\n", name);
            return true;
        }
    }
    
    fprintf(stderr, "Skill not found: %s\n", name);
    return false;
}

// Find a skill by name
Skill *skill_find(const char *name) {
    for (int i = 0; i < g_skill_registry.count; i++) {
        Skill *skill = g_skill_registry.skills[i];
        if (skill && strcmp(skill->name, name) == 0) {
            return skill;
        }
    }
    return NULL;
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
            printf("  %s (%s) - %s\n", skill->name, skill->version, skill->description);
            printf("    Author: %s\n", skill->author);
            printf("    Category: %s\n", skill->category);
            printf("    Status: %s\n", skill->enabled ? "Enabled" : "Disabled");
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