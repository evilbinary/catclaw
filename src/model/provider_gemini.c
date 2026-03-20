#include "ai_provider.h"
#include "ai_provider_factory.h"
#include "common/cJSON.h"
#include "common/log.h"
#include <stdlib.h>
#include <string.h>

// Gemini Provider (骨架)
typedef struct {
    bool initialized;
} GeminiProviderData;

static bool gemini_init(AIProvider* self) {
    GeminiProviderData* data = (GeminiProviderData*)calloc(1, sizeof(GeminiProviderData));
    self->impl = data;
    log_info("Gemini Provider initialized: model=%s", 
             self->config.model_name ? self->config.model_name : "gemini-pro");
    return true;
}

static void gemini_destroy(AIProvider* self) {
    free(self->impl);
}

static AIProviderResponse* gemini_send_messages(AIProvider* self,
                                                 MessageList* messages,
                                                 const char* system_prompt) {
    // TODO: 实现 Gemini API
    char mock[256];
    snprintf(mock, sizeof(mock), "[Mock Gemini] Response for %d messages", 
             messages ? messages->count : 0);
    return ai_provider_response_create(mock, true, NULL);
}

AIProvider* provider_gemini_create(const AIProviderConfig* config) {
    AIProvider* provider = (AIProvider*)calloc(1, sizeof(AIProvider));
    provider->name = "gemini";
    
    if (config) {
        provider->config.api_key = config->api_key ? strdup(config->api_key) : NULL;
        provider->config.model_name = config->model_name ? strdup(config->model_name) : strdup("gemini-pro");
        provider->config.base_url = config->base_url ? strdup(config->base_url) : NULL;
        provider->config.temperature = config->temperature;
        provider->config.max_tokens = config->max_tokens;
        provider->config.stream = config->stream;
    }
    
    provider->init = gemini_init;
    provider->destroy = gemini_destroy;
    provider->send_messages = gemini_send_messages;
    provider->free_response = ai_provider_response_destroy;
    
    provider->init(provider);
    return provider;
}
