#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>

// Configuration structure
typedef struct {
    char *model;
    int gateway_port;
    char *workspace_dir;
    bool browser_enabled;
} Config;

// Global config instance
extern Config g_config;

// Functions
bool config_load(void);
void config_cleanup(void);
void config_print(void);

#endif // CONFIG_H
