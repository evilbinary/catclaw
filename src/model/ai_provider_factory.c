#include "ai_provider_factory.h"
#include "common/log.h"
#include <stdlib.h>
#include <string.h>

// 外部提供者创建函数声明
extern AIProvider* provider_openai_create(const AIProviderConfig* config);
extern AIProvider* provider_anthropic_create(const AIProviderConfig* config);
extern AIProvider* provider_gemini_create(const AIProviderConfig* config);
extern AIProvider* provider_ollama_create(const AIProviderConfig* config);

// 注册表
static ProviderCreateFunc g_provider_registry[AI_PROVIDER_COUNT] = {NULL};

void ai_provider_init_registry(void) {
    // 注册内置提供者
    g_provider_registry[AI_PROVIDER_OPENAI] = provider_openai_create;
    g_provider_registry[AI_PROVIDER_ANTHROPIC] = provider_anthropic_create;
    g_provider_registry[AI_PROVIDER_GEMINI] = provider_gemini_create;
    g_provider_registry[AI_PROVIDER_OLLAMA] = provider_ollama_create;
}

AIProvider* ai_provider_create(AIProviderType type, const AIProviderConfig* config) {
    // 延迟初始化注册表
    static bool registry_initialized = false;
    if (!registry_initialized) {
        ai_provider_init_registry();
        registry_initialized = true;
    }
    
    if (type < 0 || type >= AI_PROVIDER_COUNT) {
        log_error("Invalid provider type: %d", type);
        return NULL;
    }
    
    ProviderCreateFunc create_func = g_provider_registry[type];
    if (!create_func) {
        log_error("Provider type %d not registered", type);
        return NULL;
    }
    
    return create_func(config);
}

void ai_provider_destroy(AIProvider* provider) {
    if (!provider) return;
    
    if (provider->destroy) {
        provider->destroy(provider);
    }
    
    // 释放配置
    free(provider->config.api_key);
    free(provider->config.model_name);
    free(provider->config.base_url);
    free(provider);
}

bool ai_provider_register(AIProviderType type, ProviderCreateFunc create_func) {
    if (type < 0 || type >= AI_PROVIDER_COUNT) {
        return false;
    }
    g_provider_registry[type] = create_func;
    return true;
}

AIProviderType ai_provider_type_from_string(const char* name) {
    if (!name) return AI_PROVIDER_OPENAI;
    
    if (strcasecmp(name, "openai") == 0) return AI_PROVIDER_OPENAI;
    if (strcasecmp(name, "anthropic") == 0 || strcasecmp(name, "claude") == 0) return AI_PROVIDER_ANTHROPIC;
    if (strcasecmp(name, "gemini") == 0 || strcasecmp(name, "google") == 0) return AI_PROVIDER_GEMINI;
    if (strcasecmp(name, "ollama") == 0 || strcasecmp(name, "llama") == 0) return AI_PROVIDER_OLLAMA;
    // 兼容常见 API
    if (strcasecmp(name, "doubao") == 0 || strcasecmp(name, "deepseek") == 0) return AI_PROVIDER_OPENAI;
    
    return AI_PROVIDER_OPENAI;  // 默认
}

const char* ai_provider_type_to_string(AIProviderType type) {
    switch (type) {
        case AI_PROVIDER_OPENAI: return "openai";
        case AI_PROVIDER_ANTHROPIC: return "anthropic";
        case AI_PROVIDER_GEMINI: return "gemini";
        case AI_PROVIDER_OLLAMA: return "ollama";
        default: return "unknown";
    }
}

// ==================== 配置相关 ====================

AIProviderConfig* ai_provider_config_create(void) {
    AIProviderConfig* config = (AIProviderConfig*)calloc(1, sizeof(AIProviderConfig));
    config->temperature = 0.7f;
    config->max_tokens = 4096;
    config->stream = true;
    return config;
}

void ai_provider_config_destroy(AIProviderConfig* config) {
    if (!config) return;
    free(config->api_key);
    free(config->model_name);
    free(config->base_url);
    free(config);
}

void ai_provider_config_set(AIProviderConfig* config,
                            const char* api_key,
                            const char* model_name,
                            const char* base_url,
                            float temperature,
                            int max_tokens,
                            bool stream) {
    if (!config) return;
    
    free(config->api_key);
    free(config->model_name);
    free(config->base_url);
    
    config->api_key = api_key ? strdup(api_key) : NULL;
    config->model_name = model_name ? strdup(model_name) : NULL;
    config->base_url = base_url ? strdup(base_url) : NULL;
    config->temperature = temperature > 0 ? temperature : 0.7f;
    config->max_tokens = max_tokens > 0 ? max_tokens : 4096;
    config->stream = stream;
}

// ==================== 响应相关 ====================

AIProviderResponse* ai_provider_response_create(const char* content, bool success, const char* error) {
    AIProviderResponse* resp = (AIProviderResponse*)calloc(1, sizeof(AIProviderResponse));
    if (!resp) return NULL;
    
    if (content) {
        resp->content = strdup(content);
        if (!resp->content) {
            log_error("[AIProvider] Failed to duplicate content");
        }
    }
    resp->success = success;
    if (error) {
        resp->error = strdup(error);
        if (!resp->error) {
            log_error("[AIProvider] Failed to duplicate error");
        }
    }
    resp->tool_calls = NULL;
    
    log_debug("[AIProvider] Response created: %p, content=%p (\"%.50s...\"), error=%p", 
              (void*)resp, (void*)resp->content, 
              resp->content ? resp->content : "(null)",
              (void*)resp->error);
    
    return resp;
}

void ai_provider_response_destroy(AIProviderResponse* response) {
    if (!response) return;
    
    log_debug("[AIProvider] Response destroy: %p, content=%p (\"%.50s...\"), error=%p", 
              (void*)response, (void*)response->content,
              response->content ? response->content : "(null)",
              (void*)response->error);
    
    // 防御性检查：确保指针不是野指针
    if (response->content) {
        log_debug("[AIProvider] Freeing content at %p", (void*)response->content);
        free(response->content);
        response->content = NULL;
    }
    if (response->error) {
        log_debug("[AIProvider] Freeing error at %p", (void*)response->error);
        free(response->error);
        response->error = NULL;
    }
    if (response->tool_calls) {
        log_debug("[AIProvider] Freeing tool_calls at %p", (void*)response->tool_calls);
        free(response->tool_calls);
        response->tool_calls = NULL;
    }
    log_debug("[AIProvider] Freeing response at %p", (void*)response);
    free(response);
}
