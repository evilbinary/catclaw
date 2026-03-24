#include "skill.h"
#include "common/plugin.h"
#include "common/log.h"
#include "common/http_client.h"
#include "common/cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <ctype.h>

// Windows compatibility
#ifdef _WIN32
  #include <direct.h>
  #define mkdir(path, mode) _mkdir(path)
#endif

// Cross-platform strndup (Windows doesn't have it)
#ifndef HAVE_STRNDUP
static char* strndup_impl(const char* s, size_t n) {
    size_t len = 0;
    while (len < n && s[len] != '\0') {
        len++;
    }
    char* result = (char*)malloc(len + 1);
    if (result) {
        memcpy(result, s, len);
        result[len] = '\0';
    }
    return result;
}
#define strndup(s, n) strndup_impl(s, n)
#endif

// Skill directories (loaded in order: low priority first)
#define SKILL_DIR_BUILTIN   NULL  // Built-in skills are registered programmatically
#define SKILL_DIR_PLUGIN    "skills"
#define SKILL_DIR_LOCAL     "local_skills"
#define MAX_PATH_LEN        512
#define MAX_FILE_SIZE       (1024 * 1024)  // 1MB max file size

// Global skill registry
static SkillRegistry g_skill_registry = {
    .skills = NULL,
    .count = 0,
    .capacity = 0
};

// Forward declarations
static bool skill_add_to_registry(Skill *skill);
static void skill_free(Skill *skill);

// Get skill source name string
const char *skill_source_name(SkillSource source) {
    switch (source) {
        case SKILL_SOURCE_BUILTIN:   return "builtin";
        case SKILL_SOURCE_PLUGIN:    return "plugin";
        case SKILL_SOURCE_LOCAL:     return "local";
        case SKILL_SOURCE_WORKSPACE: return "workspace";
        case SKILL_SOURCE_HUB:       return "hub";
        default: return "unknown";
    }
}

// Get skill type name string
const char *skill_type_name(SkillType type) {
    switch (type) {
        case SKILL_TYPE_PLUGIN:     return "plugin";
        case SKILL_TYPE_MARKDOWN:   return "markdown";
        case SKILL_TYPE_EXECUTABLE: return "executable";
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

// Get hub cache directory path
static char *get_hub_cache_path(void) {
    char *home = getenv("HOME");
    if (!home) {
        return NULL;
    }
    
    char *path = malloc(MAX_PATH_LEN);
    if (!path) {
        return NULL;
    }
    
    snprintf(path, MAX_PATH_LEN, "%s/.catclaw/hub_skills", home);
    return path;
}

// Internal recursive function to load skills from directory
static int skill_load_from_dir_recursive(const char *dir_path, SkillSource source, int depth) {
    if (!dir_path || depth > 5) {  // Max recursion depth
        return 0;
    }
    
    DIR *dir = opendir(dir_path);
    if (!dir) {
        return 0;
    }
    
    int loaded = 0;
    struct dirent *entry;
    
    while ((entry = readdir(dir)) != NULL) {
        // Skip hidden files and directories
        if (entry->d_name[0] == '.') {
            continue;
        }
        
        // Build full path
        char full_path[MAX_PATH_LEN];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        
        // Check file/directory status
        struct stat st;
        if (stat(full_path, &st) != 0) {
            continue;
        }
        
        // If directory, recurse into it
        if (S_ISDIR(st.st_mode)) {
            log_debug("[Skill] Entering subdirectory: %s", full_path);
            loaded += skill_load_from_dir_recursive(full_path, source, depth + 1);
            continue;
        }
        
        // Skip non-regular files
        if (!S_ISREG(st.st_mode)) {
            continue;
        }
        
        // Check file extension
        const char *ext = strrchr(entry->d_name, '.');
        if (!ext) {
            continue;
        }
        
        // Load based on file type
        bool success = false;
        if (strcmp(ext, ".so") == 0) {
            // Plugin skill
            log_info("[Skill] Auto-loading %s plugin: %s", skill_source_name(source), full_path);
            success = skill_load_from_source(full_path, source);
        } else if (strcmp(ext, ".md") == 0) {
            // Markdown skill
            log_info("[Skill] Auto-loading %s markdown: %s", skill_source_name(source), full_path);
            success = skill_load_markdown(full_path, source);
        }
        
        if (success) {
            loaded++;
        } else {
            log_warn("[Skill] Failed to load: %s", full_path);
        }
    }
    
    closedir(dir);
    return loaded;
}

// Auto-load skills from directory with specified source type (recursive)
static int skill_auto_load_from_dir(const char *dir_path, SkillSource source) {
    if (!dir_path) {
        return 0;
    }
    
    log_info("[Skill] Scanning directory (recursive): %s", dir_path);
    
    int loaded = skill_load_from_dir_recursive(dir_path, source, 0);
    
    log_info("[Skill] Loaded %d skill(s) from %s", loaded, dir_path);
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
        skill_free(g_skill_registry.skills[i]);
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
                skill_free(existing);
                
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
                skill_free(new_skill);
                
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
                skill_free(existing);
                
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
    Skill *skill = calloc(1, sizeof(Skill));
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
    skill->type = SKILL_TYPE_PLUGIN;
    skill->path = strdup(path);
    skill->tags = NULL;
    skill->prompt_template = NULL;
    skill->examples = NULL;
    skill->hub_url = NULL;
    skill->hub_id = NULL;
    
    if (skill_add_to_registry(skill)) {
        log_info("[Skill] Loaded: %s [%s/%s]", skill->name, 
                 skill_source_name(source), skill_type_name(skill->type));
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
    
    Skill *skill = calloc(1, sizeof(Skill));
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
    skill->type = SKILL_TYPE_PLUGIN;
    skill->path = NULL;
    skill->tags = NULL;
    skill->prompt_template = NULL;
    skill->examples = NULL;
    skill->hub_url = NULL;
    skill->hub_id = NULL;
    
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
            if (skill->type == SKILL_TYPE_PLUGIN && skill->path) {
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
            skill_free(skill);
            
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
    
    // Execute based on skill type
    switch (skill->type) {
        case SKILL_TYPE_PLUGIN:
            if (!skill->execute) {
                fprintf(stderr, "Skill has no execute function: %s\n", name);
                return strdup("Error: Skill has no execute function");
            }
            return skill->execute(params);
            
        case SKILL_TYPE_MARKDOWN:
            return skill_execute_markdown(skill, params);
            
        default:
            return strdup("Error: Unknown skill type");
    }
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
            printf("  %s (%s) [%s/%s] - %s\n", 
                   skill->name, skill->version, 
                   skill_source_name(skill->source),
                   skill_type_name(skill->type),
                   skill->description);
            printf("    Author: %s | Category: %s | %s\n", 
                   skill->author, skill->category,
                   skill->enabled ? "✓ Enabled" : "✗ Disabled");
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

// Free a skill structure
static void skill_free(Skill *skill) {
    if (!skill) return;
    
    free(skill->name);
    free(skill->description);
    free(skill->version);
    free(skill->author);
    free(skill->category);
    free(skill->tags);
    free(skill->path);
    free(skill->prompt_template);
    free(skill->examples);
    free(skill->hub_url);
    free(skill->hub_id);
    free(skill);
}

//==================== Markdown Skill Functions ====================

// Parse markdown front matter (YAML-like header)
static char *parse_md_front_matter(const char *content, const char *key) {
    // Look for ---\nkey: value\n---
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\n%s:", key);
    
    const char *start = strstr(content, pattern);
    if (!start) return NULL;
    
    start += strlen(pattern);
    while (*start == ' ' || *start == '\t') start++;
    
    const char *end = strchr(start, '\n');
    if (!end) return NULL;
    
    size_t len = end - start;
    char *value = malloc(len + 1);
    if (value) {
        strncpy(value, start, len);
        value[len] = '\0';
    }
    return value;
}

// Load a markdown skill
bool skill_load_markdown(const char *path, SkillSource source) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "Cannot open markdown skill: %s\n", path);
        return false;
    }
    
    // Read file content
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    if (file_size > MAX_FILE_SIZE) {
        fclose(fp);
        fprintf(stderr, "Markdown skill too large: %s\n", path);
        return false;
    }
    
    char *content = malloc(file_size + 1);
    if (!content) {
        fclose(fp);
        return false;
    }
    
    size_t read_size = fread(content, 1, file_size, fp);
    content[read_size] = '\0';
    fclose(fp);
    
    // Check for front matter
    if (strncmp(content, "---\n", 4) != 0) {
        free(content);
        fprintf(stderr, "Invalid markdown skill format (missing front matter): %s\n", path);
        return false;
    }
    
    // Parse front matter
    char *name = parse_md_front_matter(content, "name");
    if (!name) {
        // Use filename as name
        const char *filename = strrchr(path, '/');
        filename = filename ? filename + 1 : path;
        char *dot = strchr(filename, '.');
        if (dot) {
            name = strndup(filename, dot - filename);
        } else {
            name = strdup(filename);
        }
    }
    
    // Create skill structure
    Skill *skill = calloc(1, sizeof(Skill));
    if (!skill) {
        free(content);
        free(name);
        return false;
    }
    
    skill->name = name;
    skill->description = parse_md_front_matter(content, "description");
    if (!skill->description) skill->description = strdup("");
    
    skill->version = parse_md_front_matter(content, "version");
    if (!skill->version) skill->version = strdup("1.0");
    
    skill->author = parse_md_front_matter(content, "author");
    if (!skill->author) skill->author = strdup("Unknown");
    
    skill->category = parse_md_front_matter(content, "category");
    if (!skill->category) skill->category = strdup("General");
    
    skill->tags = parse_md_front_matter(content, "tags");
    
    // Find prompt template (after ---\n---)
    const char *body = strstr(content + 4, "\n---\n");
    if (body) {
        body += 5;  // Skip ---\n
        skill->prompt_template = strdup(body);
    } else {
        skill->prompt_template = strdup(content);
    }
    
    skill->examples = parse_md_front_matter(content, "examples");
    
    skill->type = SKILL_TYPE_MARKDOWN;
    skill->source = source;
    skill->path = strdup(path);
    skill->enabled = true;
    skill->execute = NULL;  // MD skills use skill_execute_markdown
    
    free(content);
    
    if (skill_add_to_registry(skill)) {
        log_info("[Skill] Loaded markdown: %s [%s]", skill->name, skill_source_name(source));
        return true;
    }
    
    return false;
}

// Execute a markdown skill (returns prompt with params)
char *skill_execute_markdown(Skill *skill, const char *params) {
    if (!skill || !skill->prompt_template) {
        return strdup("Error: Invalid markdown skill");
    }
    
    // If no params, return the template as-is
    if (!params || strlen(params) == 0) {
        return strdup(skill->prompt_template);
    }
    
    // Replace {{params}} placeholder in template
    const char *placeholder = "{{params}}";
    size_t result_size = strlen(skill->prompt_template) + strlen(params) + 256;
    char *result = malloc(result_size);
    if (!result) {
        return strdup("Error: Memory allocation failed");
    }
    
    // Simple replacement
    const char *pos = strstr(skill->prompt_template, placeholder);
    if (pos) {
        size_t prefix_len = pos - skill->prompt_template;
        snprintf(result, result_size, "%.*s%s%s",
                 (int)prefix_len, skill->prompt_template,
                 params,
                 pos + strlen(placeholder));
    } else {
        // No placeholder, append params
        snprintf(result, result_size, "%s\n\nUser input: %s", skill->prompt_template, params);
    }
    
    return result;
}

//==================== Skill Discovery Functions ====================

// Case-insensitive substring search
static bool str_contains_ci(const char *haystack, const char *needle) {
    if (!haystack || !needle) return false;
    
    char *haystack_lower = strdup(haystack);
    char *needle_lower = strdup(needle);
    
    for (char *p = haystack_lower; *p; p++) *p = tolower(*p);
    for (char *p = needle_lower; *p; p++) *p = tolower(*p);
    
    bool found = strstr(haystack_lower, needle_lower) != NULL;
    
    free(haystack_lower);
    free(needle_lower);
    return found;
}

// Calculate relevance score for a skill match
static int calculate_relevance(Skill *skill, const char *query) {
    int score = 0;
    
    // Exact name match: 100
    if (strcmp(skill->name, query) == 0) {
        return 100;
    }
    
    // Name starts with query: 80
    if (strncasecmp(skill->name, query, strlen(query)) == 0) {
        score = 80;
    }
    // Name contains query: 60
    else if (str_contains_ci(skill->name, query)) {
        score = 60;
    }
    
    // Description match: +30
    if (skill->description && str_contains_ci(skill->description, query)) {
        score = (score > 0) ? score + 30 : 40;
    }
    
    // Category match: +20
    if (skill->category && str_contains_ci(skill->category, query)) {
        score = (score > 0) ? score + 20 : 30;
    }
    
    // Tags match: +15
    if (skill->tags && str_contains_ci(skill->tags, query)) {
        score = (score > 0) ? score + 15 : 25;
    }
    
    return score;
}

// Determine which field matched
static char *get_matched_field(Skill *skill, const char *query) {
    if (strcmp(skill->name, query) == 0 || str_contains_ci(skill->name, query)) {
        return strdup("name");
    }
    if (skill->description && str_contains_ci(skill->description, query)) {
        return strdup("description");
    }
    if (skill->category && str_contains_ci(skill->category, query)) {
        return strdup("category");
    }
    if (skill->tags && str_contains_ci(skill->tags, query)) {
        return strdup("tags");
    }
    return strdup("unknown");
}

// Discover skills by query (for agent auto-discovery)
SkillMatchResult *skill_discover(const char *query) {
    if (!query) return NULL;
    
    SkillMatchResult *result = calloc(1, sizeof(SkillMatchResult));
    if (!result) return NULL;
    
    result->capacity = 20;
    result->matches = calloc(result->capacity, sizeof(SkillMatch));
    result->count = 0;
    
    // Search all registered skills
    for (int i = 0; i < g_skill_registry.count; i++) {
        Skill *skill = g_skill_registry.skills[i];
        if (!skill || !skill->enabled) continue;
        
        int relevance = calculate_relevance(skill, query);
        if (relevance > 0) {
            // Expand array if needed
            if (result->count >= result->capacity) {
                result->capacity *= 2;
                result->matches = realloc(result->matches, 
                                          sizeof(SkillMatch) * result->capacity);
            }
            
            result->matches[result->count].skill = skill;
            result->matches[result->count].relevance = relevance;
            result->matches[result->count].matched_by = get_matched_field(skill, query);
            result->count++;
        }
    }
    
    // Sort by relevance (bubble sort - simple for small arrays)
    for (int i = 0; i < result->count - 1; i++) {
        for (int j = i + 1; j < result->count; j++) {
            if (result->matches[j].relevance > result->matches[i].relevance) {
                SkillMatch temp = result->matches[i];
                result->matches[i] = result->matches[j];
                result->matches[j] = temp;
            }
        }
    }
    
    return result;
}

// Free match result
void skill_match_result_free(SkillMatchResult *result) {
    if (!result) return;
    
    for (int i = 0; i < result->count; i++) {
        free(result->matches[i].matched_by);
    }
    free(result->matches);
    free(result);
}

// Search local skills with fuzzy matching
SkillMatchResult *skill_search_local(const char *query, int limit) {
    if (!query) return NULL;
    
    SkillMatchResult *result = skill_discover(query);
    if (!result) return NULL;
    
    // Apply limit
    if (limit > 0 && result->count > limit) {
        // Free excess matches
        for (int i = limit; i < result->count; i++) {
            free(result->matches[i].matched_by);
        }
        result->count = limit;
    }
    
    return result;
}

// Get detailed skill info (lazy load for markdown skills)
char *skill_get_detailed(const char *name) {
    Skill *skill = skill_find(name);
    if (!skill) {
        return strdup("Error: Skill not found");
    }
    
    // Build detailed info string
    char *info = malloc(2048);
    if (!info) return strdup("Error: Memory allocation failed");
    
    int len = snprintf(info, 2048,
        "Skill: %s\n"
        "────────────────────────────────────────\n"
        "  Name:        %s\n"
        "  Version:     %s\n"
        "  Author:      %s\n"
        "  Category:    %s\n"
        "  Tags:        %s\n"
        "  Source:      %s\n"
        "  Type:        %s\n"
        "  Path:        %s\n"
        "  Status:      %s\n"
        "────────────────────────────────────────\n"
        "Description:\n  %s\n",
        skill->name,
        skill->name,
        skill->version ? skill->version : "N/A",
        skill->author ? skill->author : "Unknown",
        skill->category ? skill->category : "General",
        skill->tags ? skill->tags : "None",
        skill_source_name(skill->source),
        skill_type_name(skill->type),
        skill->path ? skill->path : "N/A",
        skill->enabled ? "Enabled" : "Disabled",
        skill->description ? skill->description : "No description"
    );
    
    // For markdown skills, include prompt template preview
    if (skill->type == SKILL_TYPE_MARKDOWN && skill->prompt_template) {
        int template_len = strlen(skill->prompt_template);
        int preview_len = template_len > 500 ? 500 : template_len;
        
        char *new_info = realloc(info, len + preview_len + 100);
        if (new_info) {
            info = new_info;
            len += snprintf(info + len, 2048 - len + preview_len + 100,
                "\n────────────────────────────────────────\n"
                "Prompt Template (preview):\n%.*s%s\n",
                preview_len, skill->prompt_template,
                template_len > 500 ? "\n... (truncated)" : ""
            );
        }
    }
    
    // For plugin skills, show execute function status
    if (skill->type == SKILL_TYPE_PLUGIN) {
        len += snprintf(info + len, 2048 - len,
            "\n────────────────────────────────────────\n"
            "Execute Function: %s\n",
            skill->execute ? "Available" : "Not Available"
        );
    }
    
    return info;
}

// Preview skill content (limited lines)
char *skill_preview(const char *name, int max_lines) {
    Skill *skill = skill_find(name);
    if (!skill) {
        return strdup("Error: Skill not found");
    }
    
    char *preview = malloc(1024);
    if (!preview) return strdup("Error: Memory allocation failed");
    
    int len = 0;
    
    // Show basic info
    len = snprintf(preview, 1024,
        "📌 %s [%s/%s]\n"
        "   %s\n",
        skill->name,
        skill_source_name(skill->source),
        skill_type_name(skill->type),
        skill->description ? skill->description : "No description"
    );
    
    // For markdown skills, show first few lines of template
    if (skill->type == SKILL_TYPE_MARKDOWN && skill->prompt_template) {
        len += snprintf(preview + len, 1024 - len, "\n   Template Preview:\n");
        
        const char *line = skill->prompt_template;
        int line_count = 0;
        while (*line && line_count < max_lines && len < 1000) {
            const char *next = strchr(line, '\n');
            if (!next) next = line + strlen(line);
            
            int line_len = next - line;
            if (line_len > 60) line_len = 60;  // Truncate long lines
            
            len += snprintf(preview + len, 1024 - len, "   │ %.*s\n", line_len, line);
            line = next + 1;
            line_count++;
        }
        
        if (line_count >= max_lines) {
            len += snprintf(preview + len, 1024 - len, "   └ ... (more lines)\n");
        }
    }
    
    return preview;
}

// Get skill prompt template
const char *skill_get_prompt_template(const char *name) {
    Skill *skill = skill_find(name);
    if (!skill || skill->type != SKILL_TYPE_MARKDOWN) {
        return NULL;
    }
    return skill->prompt_template;
}

//==================== Hub Skill Functions ====================

// Parse hub skill JSON response
static HubSkillInfo *parse_hub_skill_json(cJSON *json) {
    if (!json) return NULL;
    
    HubSkillInfo *info = calloc(1, sizeof(HubSkillInfo));
    if (!info) return NULL;
    
    cJSON *item;
    
    item = cJSON_GetObjectItem(json, "id");
    info->id = item ? strdup(item->valuestring) : NULL;
    
    item = cJSON_GetObjectItem(json, "name");
    info->name = item ? strdup(item->valuestring) : NULL;
    
    item = cJSON_GetObjectItem(json, "description");
    info->description = item ? strdup(item->valuestring) : NULL;
    
    item = cJSON_GetObjectItem(json, "author");
    info->author = item ? strdup(item->valuestring) : NULL;
    
    item = cJSON_GetObjectItem(json, "version");
    info->version = item ? strdup(item->valuestring) : NULL;
    
    item = cJSON_GetObjectItem(json, "category");
    info->category = item ? strdup(item->valuestring) : NULL;
    
    item = cJSON_GetObjectItem(json, "tags");
    info->tags = item ? strdup(item->valuestring) : NULL;
    
    item = cJSON_GetObjectItem(json, "download_url");
    info->download_url = item ? strdup(item->valuestring) : NULL;
    
    item = cJSON_GetObjectItem(json, "preview_url");
    info->preview_url = item ? strdup(item->valuestring) : NULL;
    
    return info;
}

static void free_hub_skill_info(HubSkillInfo *info) {
    if (!info) return;
    free(info->id);
    free(info->name);
    free(info->description);
    free(info->author);
    free(info->version);
    free(info->category);
    free(info->tags);
    free(info->download_url);
    free(info->preview_url);
    free(info);
}

// List skills from hub
bool skill_hub_list(int page, int limit) {
    char url[256];
    snprintf(url, sizeof(url), "%s/api/skills?page=%d&limit=%d", SKILL_HUB_URL, page, limit);
    
    printf("Fetching skills from hub: %s\n", url);
    
    HttpResponse *response = http_get(url);
    if (!response || response->status_code != 200) {
        fprintf(stderr, "Failed to fetch skills from hub\n");
        if (response) http_response_free(response);
        return false;
    }
    
    cJSON *json = cJSON_Parse(response->body);
    if (!json) {
        fprintf(stderr, "Invalid JSON response from hub\n");
        http_response_free(response);
        return false;
    }
    
    cJSON *skills = cJSON_GetObjectItem(json, "skills");
    if (!skills || !cJSON_IsArray(skills)) {
        fprintf(stderr, "No skills array in response\n");
        cJSON_Delete(json);
        http_response_free(response);
        return false;
    }
    
    printf("\n📦 Hub Skills (Page %d):\n", page);
    printf("─────────────────────────────────────────────────────────\n");
    
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, skills) {
        HubSkillInfo *info = parse_hub_skill_json(item);
        if (info) {
            printf("  📌 %s (v%s)\n", info->name ? info->name : "Unknown", 
                   info->version ? info->version : "0.0");
            printf("     ID: %s\n", info->id ? info->id : "N/A");
            printf("     %s\n", info->description ? info->description : "No description");
            printf("     Author: %s | Category: %s\n", 
                   info->author ? info->author : "Unknown",
                   info->category ? info->category : "General");
            printf("\n");
            free_hub_skill_info(info);
        }
    }
    
    cJSON_Delete(json);
    http_response_free(response);
    return true;
}

// Search skills on hub
bool skill_hub_search(const char *query) {
    if (!query) return false;
    
    char url[512];
    char encoded_query[256];
    // Simple URL encoding for spaces
    char *p = encoded_query;
    for (const char *q = query; *q && p < encoded_query + sizeof(encoded_query) - 4; q++) {
        if (*q == ' ') {
            *p++ = '%'; *p++ = '2'; *p++ = '0';
        } else if (*q == '&') {
            *p++ = '%'; *p++ = '2'; *p++ = '6';
        } else {
            *p++ = *q;
        }
    }
    *p = '\0';
    
    snprintf(url, sizeof(url), "%s/api/skills/search?q=%s", SKILL_HUB_URL, encoded_query);
    
    printf("Searching hub for: %s\n", query);
    
    HttpResponse *response = http_get(url);
    if (!response || response->status_code != 200) {
        fprintf(stderr, "Search failed\n");
        if (response) http_response_free(response);
        return false;
    }
    
    cJSON *json = cJSON_Parse(response->body);
    if (!json) {
        http_response_free(response);
        return false;
    }
    
    cJSON *results = cJSON_GetObjectItem(json, "results");
    if (!results || !cJSON_IsArray(results)) {
        printf("No results found.\n");
        cJSON_Delete(json);
        http_response_free(response);
        return true;
    }
    
    int count = cJSON_GetArraySize(results);
    printf("\n🔍 Found %d result(s) for '%s':\n", count, query);
    printf("─────────────────────────────────────────────────────────\n");
    
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, results) {
        HubSkillInfo *info = parse_hub_skill_json(item);
        if (info) {
            printf("  📌 %s [%s]\n", info->name, info->id);
            printf("     %s\n", info->description);
            printf("\n");
            free_hub_skill_info(info);
        }
    }
    
    cJSON_Delete(json);
    http_response_free(response);
    return true;
}

// Get skill info from hub
bool skill_hub_info(const char *skill_id) {
    if (!skill_id) return false;
    
    char url[512];
    snprintf(url, sizeof(url), "%s/api/skills/%s", SKILL_HUB_URL, skill_id);
    
    HttpResponse *response = http_get(url);
    if (!response || response->status_code != 200) {
        fprintf(stderr, "Skill not found: %s\n", skill_id);
        if (response) http_response_free(response);
        return false;
    }
    
    cJSON *json = cJSON_Parse(response->body);
    if (!json) {
        http_response_free(response);
        return false;
    }
    
    HubSkillInfo *info = parse_hub_skill_json(json);
    if (info) {
        printf("\n📌 Skill: %s\n", info->name);
        printf("─────────────────────────────────────────────────────────\n");
        printf("  ID:          %s\n", info->id);
        printf("  Version:     %s\n", info->version);
        printf("  Author:      %s\n", info->author);
        printf("  Category:    %s\n", info->category);
        printf("  Tags:        %s\n", info->tags ? info->tags : "None");
        printf("  Description: %s\n", info->description);
        printf("  Download:    /skill hub download %s\n", skill_id);
        free_hub_skill_info(info);
    }
    
    cJSON_Delete(json);
    http_response_free(response);
    return true;
}

// Download skill from hub
bool skill_hub_download(const char *skill_id) {
    if (!skill_id) return false;
    
    char url[512];
    snprintf(url, sizeof(url), "%s/api/skills/%s/download", SKILL_HUB_URL, skill_id);
    
    printf("Downloading skill: %s\n", skill_id);
    
    HttpResponse *response = http_get(url);
    if (!response || response->status_code != 200) {
        fprintf(stderr, "Download failed: %s\n", skill_id);
        if (response) http_response_free(response);
        return false;
    }
    
    // Get hub cache directory
    char *cache_dir = get_hub_cache_path();
    if (!cache_dir) {
        fprintf(stderr, "Failed to get cache directory\n");
        http_response_free(response);
        return false;
    }
    
    // Create directory if not exists
    mkdir(cache_dir, 0755);
    
    // Save to file
    char filepath[MAX_PATH_LEN];
    snprintf(filepath, sizeof(filepath), "%s/%s.md", cache_dir, skill_id);
    
    FILE *fp = fopen(filepath, "w");
    if (!fp) {
        fprintf(stderr, "Failed to create file: %s\n", filepath);
        free(cache_dir);
        http_response_free(response);
        return false;
    }
    
    fwrite(response->body, 1, response->body_len, fp);
    fclose(fp);
    
    printf("✓ Downloaded to: %s\n", filepath);
    
    free(cache_dir);
    http_response_free(response);
    return true;
}

// Install skill from hub (download and load)
bool skill_hub_install(const char *skill_id) {
    if (!skill_id) return false;
    
    // Download first
    if (!skill_hub_download(skill_id)) {
        return false;
    }
    
    // Get file path
    char *cache_dir = get_hub_cache_path();
    if (!cache_dir) return false;
    
    char filepath[MAX_PATH_LEN];
    snprintf(filepath, sizeof(filepath), "%s/%s.md", cache_dir, skill_id);
    
    // Load the skill
    bool success = skill_load_markdown(filepath, SKILL_SOURCE_HUB);
    
    free(cache_dir);
    
    if (success) {
        printf("✓ Skill installed: %s\n", skill_id);
    }
    
    return success;
}
