#include "config.h"
#include "common/cJSON.h"
#include "common/utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

// Global config instance
Config g_config = {
    // Models config defaults
    .models = {
        .models = NULL,
        .count = 0,
        .capacity = 0,
        .current_index = 0,
        .default_index = 0,
        .default_model = NULL
    },
    // Model config defaults (current selected model)
    .model = {
        .name = NULL,
        .provider = NULL,
        .model_name = NULL,
        .base_url = NULL,
        .api_key = NULL,
        .max_context_tokens = 4096,
        .timeout_seconds = 30,
        .temperature = 0.7f,
        .max_tokens = 1024
    },
    // Gateway config defaults
    .gateway = {
        .port = 18789,
        .http_port = 8080,
        .browser_enabled = false,
        .http_api_key = NULL,
        .http_auth_enabled = false,
        .http_server_enabled = false,
        .websocket_enabled = false
    },
    // Workspace config defaults
    .workspace = {
        .path = NULL
    },
    // Session config defaults
    .session = {
        .max_sessions = 100,
        .auto_save = true,
        .default_session_key = NULL,
        .max_history_per_session = 1000,
        .context_history_limit = 5
    },
    // Logging config defaults
    .logging = {
        .level = NULL,
        .file = NULL,
        .console_output = true
    },
    // Compaction config defaults
    .compaction = {
        .enabled = true,
        .threshold = 3000
    },
    // Agent config defaults
    .agent = {
        .system_prompt = NULL
    },
    // Channels config defaults
    .channels = {
        .channels = NULL,
        .count = 0,
        .capacity = 0,
        .current_index = 0,
        .default_index = 0,
        .default_channel = NULL
    },
    // Legacy fields
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
    .http_port = 8080,
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

// Parse model configuration
// Parse a single model config and add to models array
static void parse_single_model(cJSON *model_json, int index) {
    if (!model_json) return;
    
    // Ensure capacity
    if (g_config.models.count >= g_config.models.capacity) {
        int new_capacity = g_config.models.capacity == 0 ? 4 : g_config.models.capacity * 2;
        ModelConfig *new_models = realloc(g_config.models.models, new_capacity * sizeof(ModelConfig));
        if (!new_models) return;
        g_config.models.models = new_models;
        g_config.models.capacity = new_capacity;
    }
    
    ModelConfig *model = &g_config.models.models[g_config.models.count];
    memset(model, 0, sizeof(ModelConfig));
    
    // Set defaults
    model->max_context_tokens = 4096;
    model->timeout_seconds = 30;
    model->temperature = 0.7f;
    model->max_tokens = 1024;
    model->stream = true;  // 默认开启流式响应
    
    cJSON *name = cJSON_GetObjectItem(model_json, "name");
    if (name && cJSON_IsString(name)) {
        model->name = strdup(name->valuestring);
    } else {
        // Generate default name
        char default_name[32];
        snprintf(default_name, sizeof(default_name), "model_%d", index + 1);
        model->name = strdup(default_name);
    }
    
    cJSON *provider = cJSON_GetObjectItem(model_json, "provider");
    if (provider && cJSON_IsString(provider)) {
        model->provider = strdup(provider->valuestring);
    }
    
    cJSON *model_name = cJSON_GetObjectItem(model_json, "model_name");
    if (!model_name) {
        model_name = cJSON_GetObjectItem(model_json, "name");  // fallback to name field
    }
    if (model_name && cJSON_IsString(model_name)) {
        model->model_name = strdup(model_name->valuestring);
    }
    
    cJSON *base_url = cJSON_GetObjectItem(model_json, "base_url");
    if (base_url && cJSON_IsString(base_url)) {
        model->base_url = strdup(base_url->valuestring);
    }
    
    cJSON *api_key = cJSON_GetObjectItem(model_json, "api_key");
    if (api_key && cJSON_IsString(api_key)) {
        model->api_key = strdup(api_key->valuestring);
    }
    
    cJSON *max_context_tokens = cJSON_GetObjectItem(model_json, "max_context_tokens");
    if (max_context_tokens && cJSON_IsNumber(max_context_tokens)) {
        model->max_context_tokens = (int)max_context_tokens->valuedouble;
    }
    
    cJSON *timeout = cJSON_GetObjectItem(model_json, "timeout_seconds");
    if (timeout && cJSON_IsNumber(timeout)) {
        model->timeout_seconds = (int)timeout->valuedouble;
    }
    
    cJSON *temperature = cJSON_GetObjectItem(model_json, "temperature");
    if (temperature && cJSON_IsNumber(temperature)) {
        model->temperature = (float)temperature->valuedouble;
    }
    
    cJSON *max_tokens = cJSON_GetObjectItem(model_json, "max_tokens");
    if (max_tokens && cJSON_IsNumber(max_tokens)) {
        model->max_tokens = (int)max_tokens->valuedouble;
    }
    
    cJSON *stream = cJSON_GetObjectItem(model_json, "stream");
    if (stream && (cJSON_IsTrue(stream) || cJSON_IsFalse(stream))) {
        model->stream = cJSON_IsTrue(stream);
    }
    
    g_config.models.count++;
}

// Parse models array or single model config
static void parse_models_config(cJSON *models) {
    if (!models) return;
    
    // Check if this is an object with "list" and "default" fields
    cJSON *list = cJSON_GetObjectItem(models, "list");
    cJSON *default_model = cJSON_GetObjectItem(models, "default");
    
    if (list && cJSON_IsArray(list)) {
        // Parse array of models from "list" field
        int size = cJSON_GetArraySize(list);
        for (int i = 0; i < size; i++) {
            cJSON *model = cJSON_GetArrayItem(list, i);
            if (model) {
                parse_single_model(model, i);
            }
        }
        
        // Parse default model
        if (default_model && cJSON_IsString(default_model)) {
            g_config.models.default_model = strdup(default_model->valuestring);
        }
    } else if (cJSON_IsArray(models)) {
        // Direct array (backward compatibility)
        int size = cJSON_GetArraySize(models);
        for (int i = 0; i < size; i++) {
            cJSON *model = cJSON_GetArrayItem(models, i);
            if (model) {
                parse_single_model(model, i);
            }
        }
    } else if (cJSON_IsObject(models)) {
        // Single model (backward compatibility)
        parse_single_model(models, 0);
    }
}

// Find model index by name
static int find_model_index(const char *name) {
    if (!name || !g_config.models.models) return -1;
    
    for (int i = 0; i < g_config.models.count; i++) {
        if (g_config.models.models[i].name && 
            strcmp(g_config.models.models[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

// Copy model config to g_config.model (for backward compatibility)
static void sync_current_model(void) {
    if (g_config.models.count == 0) return;
    
    // Determine which model to use
    int target_index = g_config.models.current_index;
    
    // If current_index is not set or invalid, try to use default model
    if (target_index >= g_config.models.count || target_index < 0) {
        target_index = 0;
        
        // Try to find default model by name
        if (g_config.models.default_model) {
            int idx = find_model_index(g_config.models.default_model);
            if (idx >= 0) {
                target_index = idx;
                g_config.models.default_index = idx;
            }
        }
        
        g_config.models.current_index = target_index;
    }
    
    ModelConfig *src = &g_config.models.models[target_index];
    ModelConfig *dst = &g_config.model;
    
    // Free old values
    free(dst->name);
    free(dst->provider);
    free(dst->model_name);
    free(dst->base_url);
    free(dst->api_key);
    
    // Copy new values
    dst->name = src->name ? strdup(src->name) : NULL;
    dst->provider = src->provider ? strdup(src->provider) : NULL;
    dst->model_name = src->model_name ? strdup(src->model_name) : NULL;
    dst->base_url = src->base_url ? strdup(src->base_url) : NULL;
    dst->api_key = src->api_key ? strdup(src->api_key) : NULL;
    dst->max_context_tokens = src->max_context_tokens;
    dst->timeout_seconds = src->timeout_seconds;
    dst->temperature = src->temperature;
    dst->max_tokens = src->max_tokens;
    dst->stream = src->stream;
}

// Legacy function for backward compatibility
static void parse_model_config(cJSON *model) {
    parse_single_model(model, 0);
    sync_current_model();
}

// Parse gateway configuration
static void parse_gateway_config(cJSON *gateway) {
    if (!gateway) return;
    
    cJSON *port = cJSON_GetObjectItem(gateway, "port");
    if (port && cJSON_IsNumber(port)) {
        g_config.gateway.port = (int)port->valuedouble;
    }
    
    cJSON *http_port = cJSON_GetObjectItem(gateway, "http_port");
    if (http_port && cJSON_IsNumber(http_port)) {
        g_config.gateway.http_port = (int)http_port->valuedouble;
    }
    
    cJSON *browser = cJSON_GetObjectItem(gateway, "browser_enabled");
    if (browser && (cJSON_IsTrue(browser) || cJSON_IsFalse(browser))) {
        g_config.gateway.browser_enabled = cJSON_IsTrue(browser);
    }
    
    cJSON *http_api_key = cJSON_GetObjectItem(gateway, "http_api_key");
    if (http_api_key && cJSON_IsString(http_api_key)) {
        free(g_config.gateway.http_api_key);
        g_config.gateway.http_api_key = strdup(http_api_key->valuestring);
        g_config.gateway.http_auth_enabled = true;
    }
    
    cJSON *http_auth = cJSON_GetObjectItem(gateway, "http_auth_enabled");
    if (http_auth && (cJSON_IsTrue(http_auth) || cJSON_IsFalse(http_auth))) {
        g_config.gateway.http_auth_enabled = cJSON_IsTrue(http_auth);
    }
    
    cJSON *http_server = cJSON_GetObjectItem(gateway, "http_server_enabled");
    if (http_server && (cJSON_IsTrue(http_server) || cJSON_IsFalse(http_server))) {
        g_config.gateway.http_server_enabled = cJSON_IsTrue(http_server);
    }
    
    cJSON *websocket = cJSON_GetObjectItem(gateway, "websocket_enabled");
    if (websocket && (cJSON_IsTrue(websocket) || cJSON_IsFalse(websocket))) {
        g_config.gateway.websocket_enabled = cJSON_IsTrue(websocket);
    }
}

// Parse workspace configuration
static void parse_workspace_config(cJSON *workspace) {
    if (!workspace) return;

    cJSON *path = cJSON_GetObjectItem(workspace, "path");
    if (path && cJSON_IsString(path)) {
        free(g_config.workspace.path);
        // Resolve path (handles ~ expansion)
        g_config.workspace.path = resolve_path(path->valuestring);
    }
}

// Parse session configuration
static void parse_session_config(cJSON *session) {
    if (!session) return;
    
    cJSON *max_sessions = cJSON_GetObjectItem(session, "max_sessions");
    if (max_sessions && cJSON_IsNumber(max_sessions)) {
        g_config.session.max_sessions = (int)max_sessions->valuedouble;
    }
    
    cJSON *auto_save = cJSON_GetObjectItem(session, "auto_save");
    if (auto_save && (cJSON_IsTrue(auto_save) || cJSON_IsFalse(auto_save))) {
        g_config.session.auto_save = cJSON_IsTrue(auto_save);
    }
    
    cJSON *default_key = cJSON_GetObjectItem(session, "default_session_key");
    if (default_key && cJSON_IsString(default_key)) {
        free(g_config.session.default_session_key);
        g_config.session.default_session_key = strdup(default_key->valuestring);
    }
    
    cJSON *max_history = cJSON_GetObjectItem(session, "max_history_per_session");
    if (max_history && cJSON_IsNumber(max_history)) {
        g_config.session.max_history_per_session = (int)max_history->valuedouble;
    }
    
    cJSON *context_limit = cJSON_GetObjectItem(session, "context_history_limit");
    if (context_limit && cJSON_IsNumber(context_limit)) {
        g_config.session.context_history_limit = (int)context_limit->valuedouble;
    }
}

// Parse logging configuration
static void parse_logging_config(cJSON *logging) {
    if (!logging) return;
    
    cJSON *level = cJSON_GetObjectItem(logging, "level");
    if (level && cJSON_IsString(level) && level->valuestring) {
        free(g_config.logging.level);
        g_config.logging.level = strdup(level->valuestring);
        if (!g_config.logging.level) {
            fprintf(stderr, "Warning: Failed to allocate memory for log level\n");
        }
    }
    
    cJSON *file = cJSON_GetObjectItem(logging, "file");
    if (file && cJSON_IsString(file) && file->valuestring) {
        free(g_config.logging.file);
        g_config.logging.file = strdup(file->valuestring);
    }
    
    cJSON *console = cJSON_GetObjectItem(logging, "console_output");
    if (console && (cJSON_IsTrue(console) || cJSON_IsFalse(console))) {
        g_config.logging.console_output = cJSON_IsTrue(console);
    }
}

// Parse compaction configuration
static void parse_compaction_config(cJSON *compaction) {
    if (!compaction) return;
    
    cJSON *enabled = cJSON_GetObjectItem(compaction, "enabled");
    if (enabled && (cJSON_IsTrue(enabled) || cJSON_IsFalse(enabled))) {
        g_config.compaction.enabled = cJSON_IsTrue(enabled);
    }
    
    cJSON *threshold = cJSON_GetObjectItem(compaction, "threshold");
    if (threshold && cJSON_IsNumber(threshold)) {
        g_config.compaction.threshold = (int)threshold->valuedouble;
    }
}

// Parse agent configuration
static void parse_agent_config(cJSON *agent) {
    if (!agent) return;
    
    cJSON *system_prompt = cJSON_GetObjectItem(agent, "system_prompt");
    if (system_prompt && cJSON_IsString(system_prompt)) {
        free(g_config.agent.system_prompt);
        g_config.agent.system_prompt = strdup(system_prompt->valuestring);
    }
}

// Parse a single channel config
static void parse_single_channel(cJSON *channel_json, int index) {
    if (!channel_json) return;
    
    // Ensure capacity
    if (g_config.channels.count >= g_config.channels.capacity) {
        int new_capacity = g_config.channels.capacity == 0 ? 4 : g_config.channels.capacity * 2;
        ChannelConfigEntry *new_channels = realloc(g_config.channels.channels, new_capacity * sizeof(ChannelConfigEntry));
        if (!new_channels) return;
        g_config.channels.channels = new_channels;
        g_config.channels.capacity = new_capacity;
    }
    
    ChannelConfigEntry *channel = &g_config.channels.channels[g_config.channels.count];
    memset(channel, 0, sizeof(ChannelConfigEntry));
    
    // Set defaults
    channel->enabled = true;
    channel->port = 0;
    channel->enable_ssl = true;
    
    // Parse id
    cJSON *id = cJSON_GetObjectItem(channel_json, "id");
    if (id && cJSON_IsString(id)) {
        channel->id = strdup(id->valuestring);
    } else {
        // Generate default id
        char default_id[32];
        snprintf(default_id, sizeof(default_id), "channel_%d", index + 1);
        channel->id = strdup(default_id);
    }
    
    // Parse name
    cJSON *name = cJSON_GetObjectItem(channel_json, "name");
    if (name && cJSON_IsString(name)) {
        channel->name = strdup(name->valuestring);
    }
    
    // Parse type
    cJSON *type = cJSON_GetObjectItem(channel_json, "type");
    if (type && cJSON_IsString(type)) {
        channel->type = strdup(type->valuestring);
    }
    
    // Parse enabled
    cJSON *enabled = cJSON_GetObjectItem(channel_json, "enabled");
    if (enabled && (cJSON_IsTrue(enabled) || cJSON_IsFalse(enabled))) {
        channel->enabled = cJSON_IsTrue(enabled);
    }
    
    // Parse common config
    cJSON *api_key = cJSON_GetObjectItem(channel_json, "api_key");
    if (api_key && cJSON_IsString(api_key)) {
        channel->api_key = strdup(api_key->valuestring);
    }
    
    cJSON *api_secret = cJSON_GetObjectItem(channel_json, "api_secret");
    if (api_secret && cJSON_IsString(api_secret)) {
        channel->api_secret = strdup(api_secret->valuestring);
    }
    
    cJSON *server = cJSON_GetObjectItem(channel_json, "server");
    if (server && cJSON_IsString(server)) {
        channel->server = strdup(server->valuestring);
    }
    
    cJSON *port = cJSON_GetObjectItem(channel_json, "port");
    if (port && cJSON_IsNumber(port)) {
        channel->port = (int)port->valuedouble;
    }
    
    cJSON *enable_ssl = cJSON_GetObjectItem(channel_json, "enable_ssl");
    if (enable_ssl && (cJSON_IsTrue(enable_ssl) || cJSON_IsFalse(enable_ssl))) {
        channel->enable_ssl = cJSON_IsTrue(enable_ssl);
    }
    
    // Parse Feishu-specific config
    cJSON *app_id = cJSON_GetObjectItem(channel_json, "app_id");
    if (app_id && cJSON_IsString(app_id)) {
        channel->app_id = strdup(app_id->valuestring);
    }
    
    cJSON *app_secret = cJSON_GetObjectItem(channel_json, "app_secret");
    if (app_secret && cJSON_IsString(app_secret)) {
        channel->app_secret = strdup(app_secret->valuestring);
    }
    
    cJSON *webhook_url = cJSON_GetObjectItem(channel_json, "webhook_url");
    if (webhook_url && cJSON_IsString(webhook_url)) {
        channel->webhook_url = strdup(webhook_url->valuestring);
    }
    
    cJSON *receive_id = cJSON_GetObjectItem(channel_json, "receive_id");
    if (receive_id && cJSON_IsString(receive_id)) {
        channel->receive_id = strdup(receive_id->valuestring);
    }
    
    cJSON *receive_id_type = cJSON_GetObjectItem(channel_json, "receive_id_type");
    if (receive_id_type && cJSON_IsString(receive_id_type)) {
        channel->receive_id_type = strdup(receive_id_type->valuestring);
    }
    
    // Parse Feishu stream mode config (typewriter effect)
    cJSON *stream_mode = cJSON_GetObjectItem(channel_json, "stream_mode");
    if (stream_mode && (cJSON_IsTrue(stream_mode) || cJSON_IsFalse(stream_mode))) {
        channel->stream_mode = cJSON_IsTrue(stream_mode);
    }
    
    cJSON *stream_speed = cJSON_GetObjectItem(channel_json, "stream_speed");
    if (stream_speed && cJSON_IsNumber(stream_speed)) {
        channel->stream_speed = (int)stream_speed->valuedouble;
    }
    
    // Parse Feishu WebSocket-specific config
    cJSON *ws_enabled = cJSON_GetObjectItem(channel_json, "ws_enabled");
    if (ws_enabled && (cJSON_IsTrue(ws_enabled) || cJSON_IsFalse(ws_enabled))) {
        channel->ws_enabled = cJSON_IsTrue(ws_enabled);
    }
    
    cJSON *ws_domain = cJSON_GetObjectItem(channel_json, "ws_domain");
    if (ws_domain && cJSON_IsString(ws_domain)) {
        channel->ws_domain = strdup(ws_domain->valuestring);
    }
    
    cJSON *ws_ping_interval = cJSON_GetObjectItem(channel_json, "ws_ping_interval");
    if (ws_ping_interval && cJSON_IsNumber(ws_ping_interval)) {
        channel->ws_ping_interval = (int)ws_ping_interval->valuedouble;
    }
    
    cJSON *ws_reconnect_interval = cJSON_GetObjectItem(channel_json, "ws_reconnect_interval");
    if (ws_reconnect_interval && cJSON_IsNumber(ws_reconnect_interval)) {
        channel->ws_reconnect_interval = (int)ws_reconnect_interval->valuedouble;
    }
    
    cJSON *ws_max_reconnect = cJSON_GetObjectItem(channel_json, "ws_max_reconnect");
    if (ws_max_reconnect && cJSON_IsNumber(ws_max_reconnect)) {
        channel->ws_max_reconnect = (int)ws_max_reconnect->valuedouble;
    }
    
    // Parse Telegram-specific config
    cJSON *chat_id = cJSON_GetObjectItem(channel_json, "chat_id");
    if (chat_id && cJSON_IsString(chat_id)) {
        channel->chat_id = strdup(chat_id->valuestring);
    }
    
    // Parse Discord-specific config
    cJSON *channel_id = cJSON_GetObjectItem(channel_json, "channel_id");
    if (channel_id && cJSON_IsString(channel_id)) {
        channel->channel_id = strdup(channel_id->valuestring);
    }
    
    cJSON *bot_token = cJSON_GetObjectItem(channel_json, "bot_token");
    if (bot_token && cJSON_IsString(bot_token)) {
        channel->bot_token = strdup(bot_token->valuestring);
    }
    
    g_config.channels.count++;
}

// Parse channels configuration
static void parse_channels_config(cJSON *channels) {
    if (!channels) return;
    
    // Check if this is an object with "list" and "default" fields
    cJSON *list = cJSON_GetObjectItem(channels, "list");
    cJSON *default_channel = cJSON_GetObjectItem(channels, "default");
    
    if (list && cJSON_IsArray(list)) {
        // Parse array of channels from "list" field
        int size = cJSON_GetArraySize(list);
        for (int i = 0; i < size; i++) {
            cJSON *channel = cJSON_GetArrayItem(list, i);
            if (channel) {
                parse_single_channel(channel, i);
            }
        }
        
        // Parse default channel
        if (default_channel && cJSON_IsString(default_channel)) {
            g_config.channels.default_channel = strdup(default_channel->valuestring);
        }
    } else if (cJSON_IsArray(channels)) {
        // Direct array (backward compatibility)
        int size = cJSON_GetArraySize(channels);
        for (int i = 0; i < size; i++) {
            cJSON *channel = cJSON_GetArrayItem(channels, i);
            if (channel) {
                parse_single_channel(channel, i);
            }
        }
    } else if (cJSON_IsObject(channels)) {
        // Single channel (backward compatibility)
        parse_single_channel(channels, 0);
    }
}

// Find channel index by id or name
static int find_channel_index(const char *name_or_id) {
    if (!name_or_id || !g_config.channels.channels) return -1;
    
    for (int i = 0; i < g_config.channels.count; i++) {
        // Try to match by id first
        if (g_config.channels.channels[i].id &&
            strcmp(g_config.channels.channels[i].id, name_or_id) == 0) {
            return i;
        }
        // Then try to match by name
        if (g_config.channels.channels[i].name &&
            strcmp(g_config.channels.channels[i].name, name_or_id) == 0) {
            return i;
        }
    }
    return -1;
}

// Sync default channel index after loading config
static void sync_default_channel(void) {
    if (g_config.channels.count == 0) return;
    
    // If we have a default channel name, find its index
    if (g_config.channels.default_channel) {
        int idx = find_channel_index(g_config.channels.default_channel);
        if (idx >= 0) {
            g_config.channels.default_index = idx;
            g_config.channels.current_index = idx;
            return;
        }
    }
    
    // Fall back to first enabled channel
    for (int i = 0; i < g_config.channels.count; i++) {
        if (g_config.channels.channels[i].enabled) {
            g_config.channels.default_index = i;
            g_config.channels.current_index = i;
            return;
        }
    }
    
    // If no enabled channel, use first one
    g_config.channels.default_index = 0;
    g_config.channels.current_index = 0;
}

// Parse legacy configuration (for backward compatibility)
static void parse_legacy_config(cJSON *root) {
    // Parse workspace path (support both workspace_path and workspace_dir)
    cJSON *workspace = cJSON_GetObjectItem(root, "workspace_path");
    if (!workspace) {
        workspace = cJSON_GetObjectItem(root, "workspace_dir");
    }
    if (workspace && cJSON_IsString(workspace)) {
        // Resolve path (handles ~ expansion)
        char *resolved = resolve_path(workspace->valuestring);
        free(g_config.workspace_path);
        g_config.workspace_path = resolved ? strdup(resolved) : NULL;
        // Also set new config
        free(g_config.workspace.path);
        g_config.workspace.path = resolved;
    }

    // Parse model provider
    cJSON *provider = cJSON_GetObjectItem(root, "model_provider");
    if (provider && cJSON_IsString(provider)) {
        free(g_config.model_provider);
        g_config.model_provider = strdup(provider->valuestring);
        free(g_config.model.provider);
        g_config.model.provider = strdup(provider->valuestring);
    }

    // Parse model name (support both model_name and model)
    cJSON *model = cJSON_GetObjectItem(root, "model_name");
    if (!model) {
        model = cJSON_GetObjectItem(root, "model");
    }
    if (model && cJSON_IsString(model)) {
        // Parse provider/name from "provider/name" format
        char *model_str = strdup(model->valuestring);
        char *slash = strchr(model_str, '/');
        if (slash) {
            *slash = '\0';
            free(g_config.model_provider);
            g_config.model_provider = strdup(model_str);
            free(g_config.model.provider);
            g_config.model.provider = strdup(model_str);
            free(g_config.model_name);
            g_config.model_name = strdup(slash + 1);
            free(g_config.model.name);
            g_config.model.name = strdup(slash + 1);
        } else {
            free(g_config.model_name);
            g_config.model_name = strdup(model_str);
            free(g_config.model.name);
            g_config.model.name = strdup(model_str);
        }
        free(model_str);
    }

    // Parse API key
    cJSON *api_key = cJSON_GetObjectItem(root, "api_key");
    if (api_key && cJSON_IsString(api_key)) {
        free(g_config.api_key);
        g_config.api_key = strdup(api_key->valuestring);
        free(g_config.model.api_key);
        g_config.model.api_key = strdup(api_key->valuestring);
    }

    // Parse API base URL (support both api_base_url and base_url)
    cJSON *api_base_url = cJSON_GetObjectItem(root, "api_base_url");
    if (!api_base_url) {
        api_base_url = cJSON_GetObjectItem(root, "base_url");
    }
    if (api_base_url && cJSON_IsString(api_base_url)) {
        free(g_config.api_base_url);
        g_config.api_base_url = strdup(api_base_url->valuestring);
        free(g_config.model.base_url);
        g_config.model.base_url = strdup(api_base_url->valuestring);
    }

    // Parse max context tokens
    cJSON *max_tokens = cJSON_GetObjectItem(root, "max_context_tokens");
    if (max_tokens && cJSON_IsNumber(max_tokens)) {
        g_config.max_context_tokens = (int)max_tokens->valuedouble;
        g_config.model.max_context_tokens = (int)max_tokens->valuedouble;
    }

    // Parse timeout seconds
    cJSON *timeout = cJSON_GetObjectItem(root, "timeout_seconds");
    if (timeout && cJSON_IsNumber(timeout)) {
        g_config.timeout_seconds = (int)timeout->valuedouble;
        g_config.model.timeout_seconds = (int)timeout->valuedouble;
    }

    // Parse enable compaction
    cJSON *compaction = cJSON_GetObjectItem(root, "enable_compaction");
    if (compaction && cJSON_IsNumber(compaction)) {
        g_config.enable_compaction = (int)compaction->valuedouble;
        g_config.compaction.enabled = (int)compaction->valuedouble != 0;
    }

    // Parse compaction threshold
    cJSON *threshold = cJSON_GetObjectItem(root, "compaction_threshold");
    if (threshold && cJSON_IsNumber(threshold)) {
        g_config.compaction_threshold = (int)threshold->valuedouble;
        g_config.compaction.threshold = (int)threshold->valuedouble;
    }

    // Parse gateway port
    cJSON *port = cJSON_GetObjectItem(root, "gateway_port");
    if (port && cJSON_IsNumber(port)) {
        g_config.gateway_port = (int)port->valuedouble;
        g_config.gateway.port = (int)port->valuedouble;
    }

    // Parse browser enabled
    cJSON *browser = cJSON_GetObjectItem(root, "browser_enabled");
    if (browser && (cJSON_IsTrue(browser) || cJSON_IsFalse(browser))) {
        g_config.browser_enabled = cJSON_IsTrue(browser);
        g_config.gateway.browser_enabled = cJSON_IsTrue(browser);
    }
    
    // Parse debug mode
    cJSON *debug = cJSON_GetObjectItem(root, "debug");
    if (debug && (cJSON_IsTrue(debug) || cJSON_IsFalse(debug))) {
        g_config.debug = cJSON_IsTrue(debug);
    }
    
    // Parse loglevel
    cJSON *loglevel = cJSON_GetObjectItem(root, "loglevel");
    if (loglevel && cJSON_IsString(loglevel) && loglevel->valuestring) {
        free(g_config.logging.level);
        g_config.logging.level = strdup(loglevel->valuestring);
        if (!g_config.logging.level) {
            fprintf(stderr, "Warning: Failed to allocate memory for log level\n");
        }
    }
}

bool config_load(void) {
    char *home = get_home_dir();
    if (!home) {
        fprintf(stderr, "Could not determine home directory\n");
        return false;
    }

    // Load config from JSON file
    char config_path[256];
    snprintf(config_path, sizeof(config_path), "%s/.catclaw/config.json", home);

    char *config_content = read_file(config_path);
    if (config_content) {
        cJSON *root = cJSON_Parse(config_content);
        if (root) {
            // Parse new grouped configuration
            // First try to parse "models" array, then fall back to "model" single config
            cJSON *models = cJSON_GetObjectItem(root, "models");
            if (models) {
                parse_models_config(models);
                sync_current_model();
            } else {
                cJSON *model = cJSON_GetObjectItem(root, "model");
                if (model) parse_model_config(model);
            }
            
            cJSON *gateway = cJSON_GetObjectItem(root, "gateway");
            if (gateway) parse_gateway_config(gateway);
            
            cJSON *workspace = cJSON_GetObjectItem(root, "workspace");
            if (workspace) parse_workspace_config(workspace);
            
            cJSON *session = cJSON_GetObjectItem(root, "session");
            if (session) parse_session_config(session);
            
            cJSON *logging = cJSON_GetObjectItem(root, "logging");
            if (logging) parse_logging_config(logging);
            
            cJSON *compaction = cJSON_GetObjectItem(root, "compaction");
            if (compaction) parse_compaction_config(compaction);
            
            cJSON *agent = cJSON_GetObjectItem(root, "agent");
            if (agent) parse_agent_config(agent);
            
            cJSON *channels = cJSON_GetObjectItem(root, "channels");
            if (channels) {
                parse_channels_config(channels);
                sync_default_channel();
            }
            
            // Parse legacy configuration for backward compatibility
            parse_legacy_config(root);

            cJSON_Delete(root);
        } else {
            fprintf(stderr, "Error parsing config.json: %s\n", cJSON_GetErrorPtr());
            fprintf(stderr, "Please fix the JSON syntax in ~/.catclaw/config.json\n");
            free(config_content);
            return false;
        }
        free(config_content);
    } else {
        fprintf(stderr, "Warning: Could not read config.json, using defaults\n");
    }

    // Set default workspace path if not provided
    if (!g_config.workspace.path && !g_config.workspace_path) {
        char default_workspace[512];
#ifdef _WIN32
        snprintf(default_workspace, sizeof(default_workspace), ".\\workspace");
#else
        snprintf(default_workspace, sizeof(default_workspace), "./workspace");
#endif
        g_config.workspace.path = strdup(default_workspace);
        g_config.workspace_path = strdup(default_workspace);
        fprintf(stderr, "Using default workspace: %s\n", default_workspace);
    }
    
    // Set default session key if not provided
    if (!g_config.session.default_session_key) {
        g_config.session.default_session_key = strdup("default");
    }
    
    // Set default logging level if not provided
    if (!g_config.logging.level) {
        const char *default_level = g_config.debug ? "debug" : "info";
        g_config.logging.level = strdup(default_level);
        if (!g_config.logging.level) {
            fprintf(stderr, "Warning: Failed to allocate memory for default log level\n");
            // Use a static string as fallback
            g_config.logging.level = (char*)default_level;
        }
    }

    // Sync legacy fields with new fields
    if (!g_config.workspace_path && g_config.workspace.path) {
        g_config.workspace_path = strdup(g_config.workspace.path);
    }
    if (!g_config.model_provider && g_config.model.provider) {
        g_config.model_provider = strdup(g_config.model.provider);
    }
    if (!g_config.model_name && g_config.model.name) {
        g_config.model_name = strdup(g_config.model.name);
    }
    if (!g_config.api_key && g_config.model.api_key) {
        g_config.api_key = strdup(g_config.model.api_key);
    }
    if (!g_config.api_base_url && g_config.model.base_url) {
        g_config.api_base_url = strdup(g_config.model.base_url);
    }
    if (!g_config.workspace.path && g_config.workspace_path) {
        g_config.workspace.path = strdup(g_config.workspace_path);
    }
    if (!g_config.model.provider && g_config.model_provider) {
        g_config.model.provider = strdup(g_config.model_provider);
    }
    if (!g_config.model.name && g_config.model_name) {
        g_config.model.name = strdup(g_config.model_name);
    }
    if (!g_config.model.api_key && g_config.api_key) {
        g_config.model.api_key = strdup(g_config.api_key);
    }
    if (!g_config.model.base_url && g_config.api_base_url) {
        g_config.model.base_url = strdup(g_config.api_base_url);
    }
    
    g_config.gateway_port = g_config.gateway.port;
    g_config.http_port = g_config.gateway.http_port;
    g_config.browser_enabled = g_config.gateway.browser_enabled;
    g_config.enable_compaction = g_config.compaction.enabled ? 1 : 0;
    g_config.compaction_threshold = g_config.compaction.threshold;
    g_config.max_context_tokens = g_config.model.max_context_tokens;
    g_config.timeout_seconds = g_config.model.timeout_seconds;

    return true;
}

// Switch to a model by name
bool config_switch_model(const char *model_name) {
    if (!model_name || !g_config.models.models) return false;
    
    for (int i = 0; i < g_config.models.count; i++) {
        if (g_config.models.models[i].name && 
            strcmp(g_config.models.models[i].name, model_name) == 0) {
            g_config.models.current_index = i;
            sync_current_model();
            printf("Switched to model: %s\n", model_name);
            return true;
        }
    }
    printf("Model not found: %s\n", model_name);
    return false;
}

// Switch to a model by index
bool config_switch_model_by_index(int index) {
    if (index < 0 || index >= g_config.models.count) {
        printf("Invalid model index: %d (valid range: 0-%d)\n", index, g_config.models.count - 1);
        return false;
    }
    g_config.models.current_index = index;
    sync_current_model();
    printf("Switched to model %d: %s\n", index, g_config.models.models[index].name);
    return true;
}

// List all available models
void config_list_models(void) {
    printf("Available models (%d total):\n", g_config.models.count);
    for (int i = 0; i < g_config.models.count; i++) {
        ModelConfig *model = &g_config.models.models[i];
        const char *current_indicator = (i == g_config.models.current_index) ? " [current]" : "";
        const char *default_indicator = (g_config.models.default_model && 
                                         model->name && 
                                         strcmp(model->name, g_config.models.default_model) == 0) ? " [default]" : "";
        printf("  %d. %s%s%s\n", i, 
               model->name ? model->name : "(unnamed)", 
               current_indicator, 
               default_indicator);
        printf("     Provider: %s, Model: %s\n", 
               model->provider ? model->provider : "(default)",
               model->model_name ? model->model_name : "(default)");
    }
}

// Get current model name
const char* config_get_current_model_name(void) {
    if (g_config.models.count == 0) return NULL;
    if (g_config.models.current_index >= g_config.models.count) return NULL;
    return g_config.models.models[g_config.models.current_index].name;
}

void config_cleanup(void) {
    // Cleanup all models in the array
    if (g_config.models.models) {
        for (int i = 0; i < g_config.models.count; i++) {
            free(g_config.models.models[i].name);
            free(g_config.models.models[i].provider);
            free(g_config.models.models[i].model_name);
            free(g_config.models.models[i].base_url);
            free(g_config.models.models[i].api_key);
        }
        free(g_config.models.models);
    }
    
    // Cleanup default model name
    free(g_config.models.default_model);
    
    // Cleanup current model config
    free(g_config.model.name);
    free(g_config.model.provider);
    free(g_config.model.model_name);
    free(g_config.model.base_url);
    free(g_config.model.api_key);
    
    // Cleanup workspace config
    free(g_config.workspace.path);
    
    // Cleanup session config
    free(g_config.session.default_session_key);
    
    // Cleanup logging config
    free(g_config.logging.level);
    free(g_config.logging.file);
    
    // Cleanup agent config
    free(g_config.agent.system_prompt);
    
    // Cleanup channels config
    if (g_config.channels.channels) {
        for (int i = 0; i < g_config.channels.count; i++) {
            ChannelConfigEntry *ch = &g_config.channels.channels[i];
            free(ch->id);
            free(ch->name);
            free(ch->type);
            free(ch->api_key);
            free(ch->api_secret);
            free(ch->server);
            free(ch->app_id);
            free(ch->app_secret);
            free(ch->webhook_url);
            free(ch->receive_id);
            free(ch->receive_id_type);
            free(ch->chat_id);
            free(ch->channel_id);
            free(ch->bot_token);
            free(ch->ws_domain);
        }
        free(g_config.channels.channels);
    }
    free(g_config.channels.default_channel);
    free(g_config.workspace_path);
    free(g_config.model_provider);
    free(g_config.model_name);
    free(g_config.api_key);
    free(g_config.api_base_url);
    
    // Reset all to NULL
    memset(&g_config, 0, sizeof(Config));
}

void config_print(void) {
    printf("Configuration:\n");
    printf("  Model:\n");
    printf("    Provider: %s\n", g_config.model.provider ? g_config.model.provider : "(default)");
    printf("    Name: %s\n", g_config.model.name ? g_config.model.name : "(default)");
    printf("    Base URL: %s\n", g_config.model.base_url ? g_config.model.base_url : "(default)");
    printf("    Max Context Tokens: %d\n", g_config.model.max_context_tokens);
    printf("    Timeout: %d seconds\n", g_config.model.timeout_seconds);
    printf("  Gateway:\n");
    printf("    Port: %d\n", g_config.gateway.port);
    printf("    Browser Enabled: %s\n", g_config.gateway.browser_enabled ? "true" : "false");
    printf("    HTTP Server Enabled: %s\n", g_config.gateway.http_server_enabled ? "true" : "false");
    printf("    WebSocket Enabled: %s\n", g_config.gateway.websocket_enabled ? "true" : "false");
    printf("  Workspace:\n");
    printf("    Path: %s\n", g_config.workspace.path ? g_config.workspace.path : "(default)");
    printf("  Session:\n");
    printf("    Max Sessions: %d\n", g_config.session.max_sessions);
    printf("    Auto Save: %s\n", g_config.session.auto_save ? "true" : "false");
    printf("    Default Session Key: %s\n", g_config.session.default_session_key ? g_config.session.default_session_key : "default");
    printf("    Max History Per Session: %d\n", g_config.session.max_history_per_session);
    printf("    Context History Limit: %d\n", g_config.session.context_history_limit);
    printf("  Logging:\n");
    printf("    Level: %s\n", g_config.logging.level ? g_config.logging.level : "(default)");
    printf("    File: %s\n", g_config.logging.file ? g_config.logging.file : "(none)");
    printf("    Console Output: %s\n", g_config.logging.console_output ? "true" : "false");
    printf("  Compaction:\n");
    printf("    Enabled: %s\n", g_config.compaction.enabled ? "true" : "false");
    printf("    Threshold: %d\n", g_config.compaction.threshold);
    printf("  Agent:\n");
    printf("    System Prompt: %s\n", g_config.agent.system_prompt ? "(configured)" : "(default)");
    printf("  Channels:\n");
    printf("    Count: %d\n", g_config.channels.count);
    for (int i = 0; i < g_config.channels.count; i++) {
        ChannelConfigEntry *ch = &g_config.channels.channels[i];
        printf("      [%d] %s (%s) %s\n", i,
               ch->name ? ch->name : "(unnamed)",
               ch->type ? ch->type : "(unknown)",
               ch->enabled ? "enabled" : "disabled");
    }
}

bool config_set(const char *key, const char *value) {
    // TODO: Implement configuration setting
    (void)key;
    (void)value;
    return false;
}

const char *config_get(const char *key) {
    if (!key) return NULL;
    
    // Check new grouped config first
    if (strcmp(key, "model.provider") == 0) return g_config.model.provider;
    if (strcmp(key, "model.name") == 0) return g_config.model.name;
    if (strcmp(key, "model.base_url") == 0) return g_config.model.base_url;
    if (strcmp(key, "model.api_key") == 0) return g_config.model.api_key;
    if (strcmp(key, "gateway.port") == 0) return NULL; // int value
    if (strcmp(key, "workspace.path") == 0) return g_config.workspace.path;
    if (strcmp(key, "session.default_session_key") == 0) return g_config.session.default_session_key;
    if (strcmp(key, "logging.level") == 0) return g_config.logging.level;
    if (strcmp(key, "logging.file") == 0) return g_config.logging.file;
    
    // Check legacy config
    if (strcmp(key, "workspace_path") == 0) return g_config.workspace_path;
    if (strcmp(key, "model_provider") == 0) return g_config.model_provider;
    if (strcmp(key, "model_name") == 0) return g_config.model_name;
    if (strcmp(key, "api_key") == 0) return g_config.api_key;
    if (strcmp(key, "api_base_url") == 0) return g_config.api_base_url;
    if (strcmp(key, "loglevel") == 0) return g_config.logging.level;
    
    return NULL;
}

// List all configured channels
void config_list_channels(void) {
    printf("Configured channels (%d total):\n", g_config.channels.count);
    for (int i = 0; i < g_config.channels.count; i++) {
        ChannelConfigEntry *ch = &g_config.channels.channels[i];
        const char *current_indicator = (i == g_config.channels.current_index) ? " [current]" : "";
        const char *default_indicator = (i == g_config.channels.default_index) ? " [default]" : "";
        printf("  %d. [%s] %s (type: %s) %s%s%s\n", i,
               ch->id ? ch->id : "(no-id)",
               ch->name ? ch->name : "(unnamed)",
               ch->type ? ch->type : "(unknown)",
               ch->enabled ? "[enabled]" : "[disabled]",
               current_indicator,
               default_indicator);
    }
}

// Get channel count
int config_get_channel_count(void) {
    return g_config.channels.count;
}

// Get channel by index
ChannelConfigEntry* config_get_channel(int index) {
    if (index < 0 || index >= g_config.channels.count) return NULL;
    return &g_config.channels.channels[index];
}

// Get default channel
ChannelConfigEntry* config_get_default_channel(void) {
    if (g_config.channels.count == 0) return NULL;
    if (g_config.channels.default_index < 0 || 
        g_config.channels.default_index >= g_config.channels.count) {
        return &g_config.channels.channels[0];
    }
    return &g_config.channels.channels[g_config.channels.default_index];
}

// Get current channel
ChannelConfigEntry* config_get_current_channel(void) {
    if (g_config.channels.count == 0) return NULL;
    if (g_config.channels.current_index < 0 || 
        g_config.channels.current_index >= g_config.channels.count) {
        return &g_config.channels.channels[0];
    }
    return &g_config.channels.channels[g_config.channels.current_index];
}

// Switch to a channel by id or name
bool config_switch_channel(const char *channel_id_or_name) {
    if (!channel_id_or_name || !g_config.channels.channels) return false;
    
    int idx = find_channel_index(channel_id_or_name);
    if (idx >= 0) {
        g_config.channels.current_index = idx;
        printf("Switched to channel: %s\n", channel_id_or_name);
        return true;
    }
    printf("Channel not found: %s\n", channel_id_or_name);
    return false;
}
