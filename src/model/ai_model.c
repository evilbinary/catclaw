#include "ai_model.h"
#include "common/log.h"
#include <stdlib.h>
#include <string.h>

// 全局 Provider 实例
static AIProvider* g_provider = NULL;
static AIProviderConfig g_default_config = {0};

// 设置流式回调（存储在当前 provider 的配置中）
void ai_model_set_stream_callback(AIStreamCallback callback, void* user_data) {
    if (g_provider) {
        g_provider->config.stream_callback = callback;
        g_provider->config.stream_user_data = user_data;
    }
}

bool ai_model_init(const AIModelConfig* config) {
    if (g_provider) {
        log_debug("AI model already initialized");
        return true;
    }
    
    if (!config) {
        log_error("ai_model_init: config is NULL");
        return false;
    }
    
    // 确定 Provider 类型
    AIProviderType type = AI_PROVIDER_OPENAI;
    
    // 优先使用配置的 provider 字段
    if (config->provider) {
        if (strcasecmp(config->provider, "ollama") == 0) type = AI_PROVIDER_OLLAMA;
        else if (strcasecmp(config->provider, "anthropic") == 0) type = AI_PROVIDER_ANTHROPIC;
        else if (strcasecmp(config->provider, "gemini") == 0) type = AI_PROVIDER_GEMINI;
        else if (strcasecmp(config->provider, "openai") == 0) type = AI_PROVIDER_OPENAI;
    } else if (config->base_url) {
        // 根据 base_url 推断类型（向后兼容）
        if (strstr(config->base_url, "anthropic")) {
            type = AI_PROVIDER_ANTHROPIC;
        } else if (strstr(config->base_url, "gemini") || strstr(config->base_url, "generativelanguage.googleapis")) {
            type = AI_PROVIDER_GEMINI;
        } else if (strstr(config->base_url, "localhost:11434") || strstr(config->base_url, "127.0.0.1:11434")) {
            type = AI_PROVIDER_OLLAMA;
        }
    }
    
    // 创建 Provider
    g_provider = ai_provider_create(type, config);
    if (!g_provider) {
        log_error("Failed to create provider");
        return false;
    }
    
    // 保存默认配置
    if (config->api_key) g_default_config.api_key = strdup(config->api_key);
    if (config->model_name) g_default_config.model_name = strdup(config->model_name);
    if (config->base_url) g_default_config.base_url = strdup(config->base_url);
    g_default_config.temperature = config->temperature;
    g_default_config.max_tokens = config->max_tokens;
    g_default_config.stream = config->stream;
    
    log_info("AI model initialized: provider=%s, model=%s",
             ai_provider_type_to_string(type),
             config->model_name ? config->model_name : "default");
    
    return true;
}

void ai_model_cleanup(void) {
    if (g_provider) {
        ai_provider_destroy(g_provider);
        g_provider = NULL;
    }
    
    // 清理默认配置
    free(g_default_config.api_key);
    free(g_default_config.model_name);
    free(g_default_config.base_url);
    memset(&g_default_config, 0, sizeof(g_default_config));
    
    log_info("AI model cleaned up");
}

bool ai_model_set_config(const AIModelConfig* config) {
    if (!config) {
        log_error("ai_model_set_config: config is NULL");
        return false;
    }
    
    if (!g_provider) {
        return ai_model_init(config);
    }
    
    // 销毁旧 Provider，创建新 Provider
    ai_provider_destroy(g_provider);
    
    // 根据 config->provider 或 base_url 确定 provider 类型
    AIProviderType type = AI_PROVIDER_OPENAI;
    if (config->provider) {
        // 优先使用配置的 provider 字段
        if (strcasecmp(config->provider, "ollama") == 0) type = AI_PROVIDER_OLLAMA;
        else if (strcasecmp(config->provider, "anthropic") == 0) type = AI_PROVIDER_ANTHROPIC;
        else if (strcasecmp(config->provider, "gemini") == 0) type = AI_PROVIDER_GEMINI;
        else if (strcasecmp(config->provider, "openai") == 0) type = AI_PROVIDER_OPENAI;
    } else if (config->base_url) {
        // 根据 base_url 推断（向后兼容）
        if (strstr(config->base_url, "anthropic")) type = AI_PROVIDER_ANTHROPIC;
        else if (strstr(config->base_url, "gemini")) type = AI_PROVIDER_GEMINI;
        else if (strstr(config->base_url, "11434")) type = AI_PROVIDER_OLLAMA;
        else if (strstr(config->base_url, "/api/generate") || strstr(config->base_url, "/api/chat")) {
            type = AI_PROVIDER_OLLAMA;
        }
    }
    
    log_debug("ai_model_set_config: provider=%s, type=%d, base_url=%s", 
              config->provider ? config->provider : "auto", 
              type, 
              config->base_url ? config->base_url : "default");
    
    g_provider = ai_provider_create(type, config);
    return g_provider != NULL;
}

AIModelResponse* ai_model_send_message(const char* message) {
    if (!g_provider) {
        log_error("AI model not initialized");
        return ai_provider_response_create(NULL, false, "AI model not initialized");
    }
    
    // 创建单消息列表
    MessageList* msg_list = message_list_create();
    if (!msg_list) {
        return ai_provider_response_create(NULL, false, "Failed to create message list");
    }
    
    Message* msg = message_create(ROLE_USER, message);
    message_list_append(msg_list, msg);
    
    AIModelResponse* response = g_provider->send_messages(g_provider, msg_list, NULL);
    
    // 不销毁消息，因为它们会被 session 管理
    message_list_destroy(msg_list);
    
    return response;
}

AIModelResponse* ai_model_send_messages(MessageList* messages, const char* system_prompt) {
    if (!g_provider) {
        log_error("AI model not initialized");
        return ai_provider_response_create(NULL, false, "AI model not initialized");
    }
    
    return g_provider->send_messages(g_provider, messages, system_prompt);
}

void ai_model_free_response(AIModelResponse* response) {
    if (!response) return;
    
    // 直接使用 ai_provider_response_destroy，避免 g_provider 变化导致的问题
    ai_provider_response_destroy(response);
}

AIProvider* ai_model_get_provider(void) {
    return g_provider;
}

bool ai_model_switch_provider(AIProviderType type, const AIModelConfig* config) {
    if (g_provider) {
        ai_provider_destroy(g_provider);
    }
    
    const AIModelConfig* cfg = config ? config : &g_default_config;
    g_provider = ai_provider_create(type, cfg);
    
    if (g_provider) {
        log_info("Switched to provider: %s", ai_provider_type_to_string(type));
        return true;
    }
    
    log_error("Failed to switch provider");
    return false;
}
