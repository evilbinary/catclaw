#include "ai_provider.h"
#include "ai_provider_factory.h"
#include "common/cJSON.h"
#include "common/log.h"
#include "common/http_client.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define ANTHROPIC_API_PATH "v1/messages"
#define ANTHROPIC_DEFAULT_BASE_URL "https://api.anthropic.com"

// Anthropic Provider 私有数据
typedef struct {
    bool initialized;
} AnthropicProviderData;

static bool anthropic_init(AIProvider* self) {
    AnthropicProviderData* data = (AnthropicProviderData*)calloc(1, sizeof(AnthropicProviderData));
    if (!data) return false;
    
    data->initialized = true;
    self->impl = data;
    http_client_init();
    
    log_info("Anthropic Provider initialized: model=%s", 
             self->config.model_name ? self->config.model_name : "claude-3-opus");
    return true;
}

static void anthropic_destroy(AIProvider* self) {
    if (self->impl) {
        free(self->impl);
        self->impl = NULL;
    }
    http_client_cleanup();
}

// 构建 API URL: base_url + /v1/messages，如果已包含则不重复添加
static char* anthropic_build_url(const AIProvider* self) {
    const char* base = self->config.base_url ? self->config.base_url : ANTHROPIC_DEFAULT_BASE_URL;
    size_t base_len = strlen(base);
    size_t path_len = strlen(ANTHROPIC_API_PATH);
    
    // 去掉 base 末尾的 '/'
    while (base_len > 0 && base[base_len - 1] == '/') {
        base_len--;
    }
    
    // 检查 base 是否已经以 v1/messages 结尾
    if (base_len >= path_len &&
        strcmp(base + base_len - path_len, ANTHROPIC_API_PATH) == 0) {
        // 已经包含路径，直接使用
        char* url = (char*)malloc(base_len + 1);
        if (!url) return NULL;
        strncpy(url, base, base_len);
        url[base_len] = '\0';
        return url;
    }
    
    // 拼接 base_url/v1/messages
    char* url = (char*)malloc(base_len + 1 + path_len + 1);
    if (!url) return NULL;
    snprintf(url, base_len + 1 + path_len + 1, "%.*s/%s", (int)base_len, base, ANTHROPIC_API_PATH);
    return url;
}

static AIProviderResponse* anthropic_send_messages(AIProvider* self,
                                                    MessageList* messages,
                                                    const char* system_prompt) {
    char* url = anthropic_build_url(self);
    if (!url) {
        return ai_provider_response_create(NULL, false, "Failed to build API URL");
    }

    // 构建请求体
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

    // 构建请求头
    HttpHeaders* headers = http_headers_new();
    if (self->config.api_key) {
        http_headers_add(headers, "x-api-key", self->config.api_key);
    }
    http_headers_add(headers, "anthropic-version", "2023-06-01");

    log_debug("[Anthropic] Request URL: %s", url);
    log_debug("[Anthropic] Request payload: %s", payload);

    HttpResponse* http_resp = http_post_json_with_headers(url, payload, headers);
    free(payload);

    log_debug("[Anthropic] HTTP status: %d, success: %s",
        http_resp ? http_resp->status_code : 0,
        http_resp ? (http_resp->success ? "true" : "false") : "N/A");
    log_debug("[Anthropic] Response body: %s",
        http_resp && http_resp->body ? http_resp->body : "(null)");

    AIProviderResponse* response = NULL;
    if (http_resp) {
        if (http_resp->body) {
            cJSON* resp_root = cJSON_Parse(http_resp->body);
            if (resp_root) {
                cJSON* err_type = cJSON_GetObjectItem(resp_root, "error");
                if (err_type && cJSON_IsObject(err_type)) {
                    cJSON* err_msg = cJSON_GetObjectItem(err_type, "message");
                    const char* err_str = err_msg && cJSON_IsString(err_msg) 
                                          ? err_msg->valuestring : "Unknown error";
                    log_error("[Anthropic] API error: %s", err_str);
                    response = ai_provider_response_create(NULL, false, err_str);
                } else {
                    cJSON* content = cJSON_GetObjectItem(resp_root, "content");
                    if (content && cJSON_IsArray(content)) {
                        cJSON* item = cJSON_GetArrayItem(content, 0);
                        if (item) {
                            cJSON* text = cJSON_GetObjectItem(item, "text");
                            response = ai_provider_response_create(
                                text && cJSON_IsString(text) ? text->valuestring : NULL, true, NULL);
                        }
                    }
                    if (response) {
                        log_debug("[Anthropic] Parsed content: %s",
                            response->content ? response->content : "(null)");
                    }
                }
                cJSON_Delete(resp_root);
            }
            
            if (!response) {
                log_error("[Anthropic] Failed to parse response, raw: %s", http_resp->body);
                response = ai_provider_response_create(NULL, false, "Failed to parse Anthropic response");
            }
        } else {
            char err_msg[128];
            snprintf(err_msg, sizeof(err_msg), "HTTP %d: empty response body", http_resp->status_code);
            response = ai_provider_response_create(NULL, false, err_msg);
        }
        http_response_free(http_resp);
    } else {
        response = ai_provider_response_create(NULL, false, "HTTP request failed");
    }

    http_headers_free(headers);
    free(url);

    return response;
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
