#include "ai_provider.h"
#include "ai_provider_factory.h"
#include "common/cJSON.h"
#include "common/log.h"
#include <stdlib.h>
#include <string.h>

// Ollama Provider (本地模型)
typedef struct {
    bool initialized;
} OllamaProviderData;

static bool ollama_init(AIProvider* self) {
    OllamaProviderData* data = (OllamaProviderData*)calloc(1, sizeof(OllamaProviderData));
    self->impl = data;
    log_info("Ollama Provider initialized: model=%s", 
             self->config.model_name ? self->config.model_name : "llama3");
    return true;
}

static void ollama_destroy(AIProvider* self) {
    free(self->impl);
}

static AIProviderResponse* ollama_send_messages(AIProvider* self,
                                                 MessageList* messages,
                                                 const char* system_prompt) {
    // TODO: 实现 Ollama API
    char mock[256];
    snprintf(mock, sizeof(mock), "[Mock Ollama] Response for %d messages", 
             messages ? messages->count : 0);
    return ai_provider_response_create(mock, true, NULL);
}

AIProvider* provider_ollama_create(const AIProviderConfig* config) {
    AIProvider* provider = (AIProvider*)calloc(1, sizeof(AIProvider));
    provider->name = "ollama";
    
    if (config) {
        provider->config.api_key = config->api_key ? strdup(config->api_key) : NULL;
        provider->config.model_name = config->model_name ? strdup(config->model_name) : strdup("llama3");
        provider->config.base_url = config->base_url ? strdup(config->base_url) : 
                                    strdup("http://localhost:11434/api/chat");
        provider->config.temperature = config->temperature;
        provider->config.max_tokens = config->max_tokens;
        provider->config.stream = config->stream;
    }
    
    provider->init = ollama_init;
    provider->destroy = ollama_destroy;
    provider->send_messages = ollama_send_messages;
    provider->free_response = ai_provider_response_destroy;
    
    provider->init(provider);
    return provider;
}
