#ifndef SKILL_H
#define SKILL_H

#include <stdbool.h>

// Skill source types (priority: WORKSPACE > LOCAL > PLUGIN > BUILTIN)
typedef enum {
    SKILL_SOURCE_BUILTIN,    // Built-in skills (lowest priority)
    SKILL_SOURCE_PLUGIN,     // Plugin skills from skills/ directory
    SKILL_SOURCE_LOCAL,      // Local skills from ./local_skills/
    SKILL_SOURCE_WORKSPACE   // Workspace skills from ~/.catclaw/workspace/skills/ (highest priority)
} SkillSource;

// Skill structure
typedef struct {
    char *name;
    char *description;
    char *version;
    
    // Skill execution function
    char *(*execute)(const char *params);
    
    // Skill metadata
    char *author;
    char *category;
    bool enabled;
    
    // Skill source
    SkillSource source;
    char *path;  // Source file path (for plugin/workspace skills)
} Skill;

// Skill registry structure
typedef struct {
    Skill **skills;
    int count;
    int capacity;
} SkillRegistry;

// Functions
bool skill_system_init(void);
void skill_system_cleanup(void);
bool skill_load(const char *path);
bool skill_load_from_source(const char *path, SkillSource source);
bool skill_register_builtin(const char *name, const char *description,
                            const char *version, char *(*execute)(const char *));
bool skill_unload(const char *name);
Skill *skill_find(const char *name);
char *skill_execute_skill(const char *name, const char *params);
void skill_list(void);
SkillRegistry *skill_get_registry(void);
bool skill_enable(const char *name);
bool skill_disable(const char *name);

#endif // SKILL_H