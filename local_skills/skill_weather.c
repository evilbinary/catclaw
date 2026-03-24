#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

char *skill_get_name(void) { return "weather"; }
char *skill_get_description(void) { return "LOCAL weather - overrides plugin"; }
char *skill_get_version(void) { return "2.0-local"; }
char *skill_get_author(void) { return "Local User"; }
char *skill_get_category(void) { return "Local"; }

char *skill_execute(const char *params) {
    char *result = malloc(256);
    snprintf(result, 256, "[LOCAL] Weather in %s: 25°C, Custom", params ? params : "unknown");
    return result;
}

bool plugin_init(void) { printf("LOCAL weather init\n"); return true; }
void plugin_cleanup(void) { printf("LOCAL weather cleanup\n"); }
void *plugin_get_function(const char *name) {
    if (strcmp(name, "skill_get_name") == 0) return (void *)skill_get_name;
    if (strcmp(name, "skill_get_description") == 0) return (void *)skill_get_description;
    if (strcmp(name, "skill_get_version") == 0) return (void *)skill_get_version;
    if (strcmp(name, "skill_get_author") == 0) return (void *)skill_get_author;
    if (strcmp(name, "skill_get_category") == 0) return (void *)skill_get_category;
    if (strcmp(name, "skill_execute") == 0) return (void *)skill_execute;
    return NULL;
}
