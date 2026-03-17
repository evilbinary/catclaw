#include "config.h"
 #include "common/cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Global config instance
Config g_config = {
    .workspace_path = NULL,
    .model_provider = NULL,
    .model_name = NULL,
    .api_key = NULL,
    .api_base_url = NULL,
    .max_context_tokens = 4096,
    .timeout_seconds = 30,
    .enable_compaction = 1,
    .compaction_threshold = 3000,
    .gateway_port = 18789,
    .browser_enabled = false,
    .debug = true
};

char *get_home_dir(void) {
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

    // Initialize workspace path to NULL
    g_config.workspace_path = NULL;

    // Load config from JSON file
    char config_path[256];
    snprintf(config_path, sizeof(config_path), "%s/.catclaw/config.json", home);

    char *config_content = read_file(config_path);
    if (config_content) {
        cJSON *root = cJSON_Parse(config_content);
        if (root) {
            // Parse workspace path (support both workspace_path and workspace_dir)
            cJSON *workspace = cJSON_GetObjectItem(root, "workspace_path");
            if (!workspace) {
                workspace = cJSON_GetObjectItem(root, "workspace_dir");
            }
            if (workspace && cJSON_IsString(workspace)) {
                free(g_config.workspace_path);
                g_config.workspace_path = strdup(workspace->valuestring);
            }

            // Parse model provider
            cJSON *provider = cJSON_GetObjectItem(root, "model_provider");
            if (provider && cJSON_IsString(provider)) {
                g_config.model_provider = strdup(provider->valuestring);
            }

            // Parse model name (support both model_name and model)
            cJSON *model = cJSON_GetObjectItem(root, "model_name");
            if (!model) {
                model = cJSON_GetObjectItem(root, "model");
            }
            if (model && cJSON_IsString(model)) {
                g_config.model_name = strdup(model->valuestring);
            }

            // Parse API key
            cJSON *api_key = cJSON_GetObjectItem(root, "api_key");
            if (api_key && cJSON_IsString(api_key)) {
                g_config.api_key = strdup(api_key->valuestring);
            }

            // Parse API base URL (support both api_base_url and base_url)
            cJSON *api_base_url = cJSON_GetObjectItem(root, "api_base_url");
            if (!api_base_url) {
                api_base_url = cJSON_GetObjectItem(root, "base_url");
            }
            if (api_base_url && cJSON_IsString(api_base_url)) {
                g_config.api_base_url = strdup(api_base_url->valuestring);
            }

            // Set default values if not provided
            if (!g_config.model_provider) {
                g_config.model_provider = strdup("llama");
            }
            if (!g_config.model_name) {
                g_config.model_name = strdup("llama3.2");
            }

            // Parse max context tokens
            cJSON *max_tokens = cJSON_GetObjectItem(root, "max_context_tokens");
            if (max_tokens && cJSON_IsNumber(max_tokens)) {
                g_config.max_context_tokens = (int)max_tokens->valuedouble;
            }

            // Parse timeout seconds
            cJSON *timeout = cJSON_GetObjectItem(root, "timeout_seconds");
            if (timeout && cJSON_IsNumber(timeout)) {
                g_config.timeout_seconds = (int)timeout->valuedouble;
            }

            // Parse enable compaction
            cJSON *compaction = cJSON_GetObjectItem(root, "enable_compaction");
            if (compaction && cJSON_IsNumber(compaction)) {
                g_config.enable_compaction = (int)compaction->valuedouble;
            }

            // Parse compaction threshold
            cJSON *threshold = cJSON_GetObjectItem(root, "compaction_threshold");
            if (threshold && cJSON_IsNumber(threshold)) {
                g_config.compaction_threshold = (int)threshold->valuedouble;
            }

            // Parse gateway port
            cJSON *port = cJSON_GetObjectItem(root, "gateway_port");
            if (port && cJSON_IsNumber(port)) {
                g_config.gateway_port = (int)port->valuedouble;
            }

            // Parse browser enabled
            cJSON *browser = cJSON_GetObjectItem(root, "browser_enabled");
            if (browser && (cJSON_IsTrue(browser) || cJSON_IsFalse(browser))) {
                g_config.browser_enabled = cJSON_IsTrue(browser);
            }
            
            // Parse debug mode
            cJSON *debug = cJSON_GetObjectItem(root, "debug");
            if (debug && (cJSON_IsTrue(debug) || cJSON_IsFalse(debug))) {
                g_config.debug = cJSON_IsTrue(debug);
            }

            cJSON_Delete(root);
        } else {
            fprintf(stderr, "Error parsing config.json: %s\n", cJSON_GetErrorPtr());
        }
        free(config_content);
    } else {
        printf("No config.json found, using defaults\n");
        // Set default values with proper memory allocation
        g_config.model_provider = strdup("anthropic");
        g_config.model_name = strdup("claude-opus-4-6");
    }

    // Ensure workspace path is set
    if (!g_config.workspace_path) {
        char *home = get_home_dir();
        if (home) {
            char path[512];
            snprintf(path, sizeof(path), "%s/.catclaw/workspace", home);
            g_config.workspace_path = strdup(path);
        } else {
            g_config.workspace_path = strdup("./workspace");
        }
    }

    printf("Configuration loaded:\n");
    config_print();

    return true;
}

void config_cleanup(void) {
    if (g_config.workspace_path) {
        free(g_config.workspace_path);
        g_config.workspace_path = NULL;
    }
    if (g_config.model_provider) {
        free(g_config.model_provider);
        g_config.model_provider = NULL;
    }
    if (g_config.model_name) {
        free(g_config.model_name);
        g_config.model_name = NULL;
    }
    if (g_config.api_key) {
        free(g_config.api_key);
        g_config.api_key = NULL;
    }
    if (g_config.api_base_url) {
        free(g_config.api_base_url);
        g_config.api_base_url = NULL;
    }
}

void config_print(void) {
    printf("  Workspace Path: %s\n", g_config.workspace_path);
    printf("  Model Provider: %s\n", g_config.model_provider);
    printf("  Model Name: %s\n", g_config.model_name);
    printf("  API Key: %s\n", g_config.api_key ? "(set)" : "(not set)");
    printf("  API Base URL: %s\n", g_config.api_base_url ? g_config.api_base_url : "(not set)");
    printf("  Max Context Tokens: %d\n", g_config.max_context_tokens);
    printf("  Timeout Seconds: %d\n", g_config.timeout_seconds);
    printf("  Enable Compaction: %s\n", g_config.enable_compaction ? "true" : "false");
    printf("  Compaction Threshold: %d\n", g_config.compaction_threshold);
    printf("  Gateway Port: %d\n", g_config.gateway_port);
    printf("  Browser Enabled: %s\n", g_config.browser_enabled ? "true" : "false");
    printf("  Debug Mode: %s\n", g_config.debug ? "true" : "false");
}

bool config_set(const char *key, const char *value) {
    if (strcmp(key, "workspace_path") == 0) {
        if (g_config.workspace_path) {
            free(g_config.workspace_path);
        }
        g_config.workspace_path = strdup(value);
        return true;
    } else if (strcmp(key, "model_provider") == 0) {
        if (g_config.model_provider) {
            free(g_config.model_provider);
        }
        g_config.model_provider = strdup(value);
        return true;
    } else if (strcmp(key, "model_name") == 0) {
        if (g_config.model_name) {
            free(g_config.model_name);
        }
        g_config.model_name = strdup(value);
        return true;
    } else if (strcmp(key, "api_key") == 0) {
        if (g_config.api_key) {
            free(g_config.api_key);
        }
        g_config.api_key = strdup(value);
        return true;
    } else if (strcmp(key, "api_base_url") == 0) {
        if (g_config.api_base_url) {
            free(g_config.api_base_url);
        }
        g_config.api_base_url = strdup(value);
        return true;
    } else if (strcmp(key, "max_context_tokens") == 0) {
        g_config.max_context_tokens = atoi(value);
        return true;
    } else if (strcmp(key, "timeout_seconds") == 0) {
        g_config.timeout_seconds = atoi(value);
        return true;
    } else if (strcmp(key, "enable_compaction") == 0) {
        g_config.enable_compaction = atoi(value);
        return true;
    } else if (strcmp(key, "compaction_threshold") == 0) {
        g_config.compaction_threshold = atoi(value);
        return true;
    } else if (strcmp(key, "gateway_port") == 0) {
        g_config.gateway_port = atoi(value);
        return true;
    } else if (strcmp(key, "browser_enabled") == 0) {
        g_config.browser_enabled = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
        return true;
    } else if (strcmp(key, "debug") == 0) {
        g_config.debug = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
        return true;
    }
    
    return false;
}

const char *config_get(const char *key) {
    static char buffer[256];
    if (strcmp(key, "workspace_path") == 0) {
        return g_config.workspace_path;
    } else if (strcmp(key, "model_provider") == 0) {
        return g_config.model_provider;
    } else if (strcmp(key, "model_name") == 0) {
        return g_config.model_name;
    } else if (strcmp(key, "api_key") == 0) {
        return g_config.api_key;
    } else if (strcmp(key, "api_base_url") == 0) {
        return g_config.api_base_url;
    } else if (strcmp(key, "max_context_tokens") == 0) {
        snprintf(buffer, sizeof(buffer), "%d", g_config.max_context_tokens);
        return buffer;
    } else if (strcmp(key, "timeout_seconds") == 0) {
        snprintf(buffer, sizeof(buffer), "%d", g_config.timeout_seconds);
        return buffer;
    } else if (strcmp(key, "enable_compaction") == 0) {
        snprintf(buffer, sizeof(buffer), "%d", g_config.enable_compaction);
        return buffer;
    } else if (strcmp(key, "compaction_threshold") == 0) {
        snprintf(buffer, sizeof(buffer), "%d", g_config.compaction_threshold);
        return buffer;
    } else if (strcmp(key, "gateway_port") == 0) {
        snprintf(buffer, sizeof(buffer), "%d", g_config.gateway_port);
        return buffer;
    } else if (strcmp(key, "browser_enabled") == 0) {
        return g_config.browser_enabled ? "true" : "false";
    } else if (strcmp(key, "debug") == 0) {
        return g_config.debug ? "true" : "false";
    }
    
    return NULL;
}
