#include "ai_provider.h"
#include "ai_provider_factory.h"
#include "common/cJSON.h"
#include "common/log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef HAVE_CURL
#include <curl/curl.h>
#endif

// Anthropic Provider 私有数据
typedef struct {
    bool initialized;
} AnthropicProviderData;

static bool anthropic_init(AIProvider* self) {
    AnthropicProviderData* data = (AnthropicProviderData*)calloc(1, sizeof(AnthropicProviderData));
    if (!data) return false;
    
    data->initialized = true;
    self->impl = data;
    
    log_info("Anthropic Provider initialized: model=%s", 
             self->config.model_name ? self->config.model_name : "claude-3-opus");
    return true;
}

static void anthropic_destroy(AIProvider* self) {
    if (self->impl) {
        free(self->impl);
        self->impl = NULL;
    }
}

#ifdef HAVE_CURL
static size_t anthropic_write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    char** buffer = (char**)userp;
    size_t current_len = *buffer ? strlen(*buffer) : 0;
    
    char* new_buffer = (char*)realloc(*buffer, current_len + realsize + 1);
    if (!new_buffer) return 0;
    
    memcpy(new_buffer + current_len, contents, realsize);
    new_buffer[current_len + realsize] = '\0';
    *buffer = new_buffer;
    return realsize;
}
#endif

static AIProviderResponse* anthropic_send_messages(AIProvider* self,
                                                    MessageList* messages,
                                                    const char* system_prompt) {
#ifdef HAVE_CURL
    CURL* curl = curl_easy_init();
    if (!curl) {
        return ai_provider_response_create(NULL, false, "Failed to initialize curl");
    }

    char* response_buffer = (char*)calloc(1, 1);
    
    const char* url = self->config.base_url ? self->config.base_url :
                      "https://api.anthropic.com/v1/messages";

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "x-api-key: %s",
             self->config.api_key ? self->config.api_key : "");
    headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");

    // 构建请求
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", 
        self->config.model_name ? self->config.model_name : "claude-3-opus-20240229");
    cJSON_AddNumberToObject(root, "max_tokens", self->config.max_tokens);
    
    if (system_prompt) {
        cJSON_AddStringToObject(root, "system", system_prompt);
    }
    
    cJSON* msg_arr = cJSON_CreateArray();
    if (messages) {
        for (int i = 0; i < messages->count; i++) {
            Message* msg = messages->messages[i];
            cJSON* msg_obj = cJSON_CreateObject();
            const char* role = "user";
            if (msg->role == ROLE_ASSISTANT) role = "assistant";
            cJSON_AddStringToObject(msg_obj, "role", role);
            cJSON_AddStringToObject(msg_obj, "content", msg->content ? msg->content : "");
            cJSON_AddItemToArray(msg_arr, msg_obj);
        }
    }
    cJSON_AddItemToObject(root, "messages", msg_arr);

    char* payload = cJSON_Print(root);
    cJSON_Delete(root);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, anthropic_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_buffer);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

    CURLcode res = curl_easy_perform(curl);

    AIProviderResponse* response = NULL;
    if (res == CURLE_OK) {
        cJSON* root = cJSON_Parse(response_buffer);
        if (root) {
            cJSON* content = cJSON_GetObjectItem(root, "content");
            if (content && cJSON_IsArray(content)) {
                cJSON* item = cJSON_GetArrayItem(content, 0);
                if (item) {
                    cJSON* text = cJSON_GetObjectItem(item, "text");
                    response = ai_provider_response_create(
                        text && cJSON_IsString(text) ? text->valuestring : NULL, true, NULL);
                }
            }
            cJSON_Delete(root);
        }
        
        if (!response) {
            response = ai_provider_response_create(NULL, false, "Failed to parse Anthropic response");
        }
    } else {
        response = ai_provider_response_create(NULL, false, curl_easy_strerror(res));
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(payload);
    free(response_buffer);

    return response;
#else
    char mock[256];
    snprintf(mock, sizeof(mock), "[Mock Anthropic] Response for %d messages", 
             messages ? messages->count : 0);
    return ai_provider_response_create(mock, true, NULL);
#endif
}

AIProvider* provider_anthropic_create(const AIProviderConfig* config) {
    AIProvider* provider = (AIProvider*)calloc(1, sizeof(AIProvider));
    if (!provider) return NULL;
    
    provider->name = "anthropic";
    
    if (config) {
        provider->config.api_key = config->api_key ? strdup(config->api_key) : NULL;
        provider->config.model_name = config->model_name ? strdup(config->model_name) : 
                                      strdup("claude-3-opus-20240229");
        provider->config.base_url = config->base_url ? strdup(config->base_url) : NULL;
        provider->config.temperature = config->temperature;
        provider->config.max_tokens = config->max_tokens > 0 ? config->max_tokens : 4096;
        provider->config.stream = config->stream;
    }
    
    provider->init = anthropic_init;
    provider->destroy = anthropic_destroy;
    provider->send_messages = anthropic_send_messages;
    provider->free_response = ai_provider_response_destroy;
    
    if (!provider->init(provider)) {
        free(provider);
        return NULL;
    }
    
    return provider;
}
