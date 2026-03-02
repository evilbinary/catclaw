#include "config.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Global config instance
Config g_config = {
    .model = "anthropic/claude-opus-4-6",
    .gateway_port = 18789,
    .workspace_dir = NULL,
    .browser_enabled = false,
    .base_url = NULL
};

static char *get_home_dir(void) {
    char *home = getenv("HOME");
#ifdef _WIN32
    if (!home) {
        home = getenv("USERPROFILE");
    }
#endif
    return home;
}

static char *read_file(const char *path) {
    FILE *file = fopen(path, "r");
    if (!file) {
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *buffer = (char *)malloc(size + 1);
    if (!buffer) {
        fclose(file);
        return NULL;
    }

    fread(buffer, 1, size, file);
    buffer[size] = '\0';
    fclose(file);

    return buffer;
}

bool config_load(void) {
    char *home = get_home_dir();
    if (!home) {
        fprintf(stderr, "Could not determine home directory\n");
        return false;
    }

    // Set default workspace directory
    size_t len = strlen(home) + 20;
    g_config.workspace_dir = (char *)malloc(len);
    if (!g_config.workspace_dir) {
        perror("malloc");
        return false;
    }
    snprintf(g_config.workspace_dir, len, "%s/.catclaw/workspace", home);

    // Load config from JSON file
    char config_path[256];
    snprintf(config_path, sizeof(config_path), "%s/.catclaw/config.json", home);

    char *config_content = read_file(config_path);
    if (config_content) {
        cJSON *root = cJSON_Parse(config_content);
        if (root) {
            // Parse model
            cJSON *model = cJSON_GetObjectItem(root, "model");
            if (model && cJSON_IsString(model)) {
                if (g_config.model) {
                    free(g_config.model);
                }
                g_config.model = strdup(model->valuestring);
            }

            // Parse gateway port
            cJSON *port = cJSON_GetObjectItem(root, "gateway_port");
            if (port && cJSON_IsNumber(port)) {
                g_config.gateway_port = (int)port->valuedouble;
            }

            // Parse workspace directory
            cJSON *workspace = cJSON_GetObjectItem(root, "workspace_dir");
            if (workspace && cJSON_IsString(workspace)) {
                free(g_config.workspace_dir);
                g_config.workspace_dir = strdup(workspace->valuestring);
            }

            // Parse browser enabled
            cJSON *browser = cJSON_GetObjectItem(root, "browser_enabled");
            if (browser && (cJSON_IsTrue(browser) || cJSON_IsFalse(browser))) {
                g_config.browser_enabled = cJSON_IsTrue(browser);
            }

            // Parse base URL
            cJSON *base_url = cJSON_GetObjectItem(root, "base_url");
            if (base_url && cJSON_IsString(base_url)) {
                if (g_config.base_url) {
                    free(g_config.base_url);
                }
                g_config.base_url = strdup(base_url->valuestring);
            }

            cJSON_Delete(root);
        } else {
            fprintf(stderr, "Error parsing config.json: %s\n", cJSON_GetErrorPtr());
        }
        free(config_content);
    } else {
        printf("No config.json found, using defaults\n");
    }

    printf("Configuration loaded:\n");
    config_print();

    return true;
}

void config_cleanup(void) {
    if (g_config.model) {
        free(g_config.model);
        g_config.model = NULL;
    }
    if (g_config.workspace_dir) {
        free(g_config.workspace_dir);
        g_config.workspace_dir = NULL;
    }
    if (g_config.base_url) {
        free(g_config.base_url);
        g_config.base_url = NULL;
    }
}

void config_print(void) {
    printf("  Model: %s\n", g_config.model);
    printf("  Gateway Port: %d\n", g_config.gateway_port);
    printf("  Workspace Dir: %s\n", g_config.workspace_dir);
    printf("  Browser Enabled: %s\n", g_config.browser_enabled ? "true" : "false");
    printf("  Base URL: %s\n", g_config.base_url ? g_config.base_url : "(not set)");
}

bool config_set(const char *key, const char *value) {
    if (strcmp(key, "model") == 0) {
        if (g_config.model) {
            free(g_config.model);
        }
        g_config.model = strdup(value);
        return true;
    } else if (strcmp(key, "gateway_port") == 0) {
        g_config.gateway_port = atoi(value);
        return true;
    } else if (strcmp(key, "workspace_dir") == 0) {
        if (g_config.workspace_dir) {
            free(g_config.workspace_dir);
        }
        g_config.workspace_dir = strdup(value);
        return true;
    } else if (strcmp(key, "browser_enabled") == 0) {
        g_config.browser_enabled = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
        return true;
    } else if (strcmp(key, "base_url") == 0) {
        if (g_config.base_url) {
            free(g_config.base_url);
        }
        g_config.base_url = strdup(value);
        return true;
    }
    return false;
}

const char *config_get(const char *key) {
    static char buffer[256];
    if (strcmp(key, "model") == 0) {
        return g_config.model;
    } else if (strcmp(key, "gateway_port") == 0) {
        snprintf(buffer, sizeof(buffer), "%d", g_config.gateway_port);
        return buffer;
    } else if (strcmp(key, "workspace_dir") == 0) {
        return g_config.workspace_dir;
    } else if (strcmp(key, "browser_enabled") == 0) {
        return g_config.browser_enabled ? "true" : "false";
    } else if (strcmp(key, "base_url") == 0) {
        return g_config.base_url;
    }
    return NULL;
}
