#ifndef SKILL_H
#define SKILL_H

#include <stdbool.h>

// Skill source types (priority: WORKSPACE > LOCAL > PLUGIN > BUILTIN)
typedef enum {
    SKILL_SOURCE_BUILTIN,    // Built-in skills (lowest priority)
    SKILL_SOURCE_PLUGIN,     // Plugin skills from skills/ directory
    SKILL_SOURCE_LOCAL,      // Local skills from ./local_skills/
    SKILL_SOURCE_WORKSPACE,  // Workspace skills from ~/.catclaw/workspace/skills/ (highest priority)
    SKILL_SOURCE_HUB         // Downloaded from skill hub
} SkillSource;

// Skill type (how the skill is implemented)
typedef enum {
    SKILL_TYPE_PLUGIN,       // Dynamic library (.so/.dll)
    SKILL_TYPE_MARKDOWN,     // Markdown document with prompt template
    SKILL_TYPE_EXECUTABLE    // External executable
} SkillType;

// Skill structure
typedef struct {
    char *name;
    char *description;
    char *version;
    
    // Skill execution function (for PLUGIN type)
    char *(*execute)(const char *params);
    
    // Skill metadata
    char *author;
    char *category;
    char *tags;              // Comma-separated tags
    bool enabled;
    
    // Skill source and type
    SkillSource source;
    SkillType type;
    char *path;              // Source file path
    
    // For MARKDOWN type
    char *prompt_template;   // Prompt template for MD skills
    char *examples;          // Usage examples
    
    // For HUB source
    char *hub_url;           // Remote URL if from hub
    char *hub_id;            // Hub skill ID
} Skill;

// Skill registry structure
typedef struct {
    Skill **skills;
    int count;
    int capacity;
} SkillRegistry;

// Hub skill info (for remote skills)
typedef struct {
    char *id;
    char *name;
    char *description;
    char *author;
    char *version;
    char *category;
    char *tags;
    char *download_url;
    char *preview_url;
} HubSkillInfo;

// Basic skill functions
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

// Markdown skill functions
bool skill_load_markdown(const char *path, SkillSource source);
char *skill_execute_markdown(Skill *skill, const char *params);

// Hub skill functions
#define SKILL_HUB_URL "https://skills.catclaw.dev"
bool skill_hub_list(int page, int limit);
bool skill_hub_search(const char *query);
bool skill_hub_info(const char *skill_id);
bool skill_hub_download(const char *skill_id);
bool skill_hub_install(const char *skill_id);

// Utility functions
const char *skill_source_name(SkillSource source);
const char *skill_type_name(SkillType type);

#endif // SKILL_H