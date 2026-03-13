#ifndef SKILL_H
#define SKILL_H

#include <stdbool.h>

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
bool skill_unload(const char *name);
Skill *skill_find(const char *name);
char *skill_execute_skill(const char *name, const char *params);
void skill_list(void);
SkillRegistry *skill_get_registry(void);
bool skill_enable(const char *name);
bool skill_disable(const char *name);

#endif // SKILL_H