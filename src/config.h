#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>

// Configuration structure
typedef struct {
    char *model;
    int gateway_port;
    char *workspace_dir;
    bool browser_enabled;
    char *base_url;
} Config;

// Global config instance
extern Config g_config;

// Functions
bool config_load(void);
void config_cleanup(void);
void config_print(void);
bool config_set(const char *key, const char *value);
const char *config_get(const char *key);

#endif // CONFIG_H
