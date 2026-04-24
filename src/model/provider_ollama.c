#include "ai_provider.h"
#include "ai_provider_factory.h"
#include "common/cJSON.h"
#include "common/log.h"
#include "common/http_client.h"
#include <stdlib.h>
#include <string.h>

// Ollama Provider (本地模型)
typedef struct {
    bool initialized;
} OllamaProviderData;

static bool ollama_init(AIProvider* self) {
    OllamaProviderData* data = (OllamaProviderData*)calloc(1, sizeof(OllamaProviderData));
    self->impl = data;
    
    // 初始化 HTTP 客户端
    http_client_init();
    
    log_info("Ollama Provider initialized: model=%s", 
             self->config.model_name ? self->config.model_name : "llama3");
    return true;
}

static void ollama_destroy(AIProvider* self) {
    if (!self) return;
    
    // 清理 HTTP 客户端
    http_client_cleanup();
    
    free(self->impl);
}

// 构建 Ollama 格式的 prompt
static char* build_ollama_prompt(MessageList* messages, const char* system_prompt) {
    if (!messages || messages->count == 0) {
        return strdup("");
    }
    
    // 计算需要的缓冲区大小
    size_t total_len = 0;
    
    if (system_prompt && strlen(system_prompt) > 0) {
        total_len += strlen(system_prompt) + 100;
    }
    
    for (int i = 0; i < messages->count; i++) {
        Message* msg = messages->messages[i];
        if (msg->content) {
            total_len += strlen(msg->content) + 50;
        }
    }
    
    char* prompt = (char*)calloc(1, total_len + 1);
    if (!prompt) return strdup("");
    
    size_t offset = 0;
    
    // 添加 system prompt
    if (system_prompt && strlen(system_prompt) > 0) {
        offset += snprintf(prompt + offset, total_len - offset, "System: %s\n\n", system_prompt);
    }
    
    // 添加消息历史
    for (int i = 0; i < messages->count; i++) {
        Message* msg = messages->messages[i];
        if (!msg->content) continue;
        
        const char* role_prefix = "";
        switch (msg->role) {
            case ROLE_USER: role_prefix = "User: "; break;
            case ROLE_ASSISTANT: role_prefix = "Assistant: "; break;
            case ROLE_SYSTEM: role_prefix = "System: "; break;
            case ROLE_TOOL: role_prefix = "Tool: "; break;
        }
        
        offset += snprintf(prompt + offset, total_len - offset, "%s%s\n", role_prefix, msg->content);
    }
    
    // 添加 Assistant 前缀，让模型知道需要回复
    offset += snprintf(prompt + offset, total_len - offset, "Assistant: ");
    
    return prompt;
}

static AIProviderResponse* ollama_send_messages(AIProvider* self,
                                                 MessageList* messages,
                                                 const char* system_prompt) {
    if (!self || !self->impl) {
        return ai_provider_response_create(NULL, false, "Provider not initialized");
    }
    
    // 构建 URL，判断使用 /api/chat 还是 /api/generate
    bool use_chat_api = false;
    char url[512];
    if (self->config.base_url) {
        if (strstr(self->config.base_url, "/api/chat")) {
            use_chat_api = true;
            strncpy(url, self->config.base_url, sizeof(url) - 1);
        } else if (strstr(self->config.base_url, "/api/generate")) {
            strncpy(url, self->config.base_url, sizeof(url) - 1);
        } else {
            // 默认使用 /api/chat（支持 messages 格式）
            use_chat_api = true;
            snprintf(url, sizeof(url), "%s/api/chat", self->config.base_url);
        }
        url[sizeof(url) - 1] = '\0';
    } else {
        use_chat_api = true;
        strncpy(url, "http://localhost:11434/api/chat", sizeof(url) - 1);
        url[sizeof(url) - 1] = '\0';
    }
    log_debug("[Ollama] Request URL: %s", url);

    // 构建请求体
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", 
        self->config.model_name ? self->config.model_name : "llama3");
    
    if (use_chat_api) {
        // /api/chat 格式: 使用 messages 数组
        cJSON* msg_array = cJSON_CreateArray();
        
        if (system_prompt && strlen(system_prompt) > 0) {
            cJSON* sys_msg = cJSON_CreateObject();
            cJSON_AddStringToObject(sys_msg, "role", "system");
            cJSON_AddStringToObject(sys_msg, "content", system_prompt);
            cJSON_AddItemToArray(msg_array, sys_msg);
        }
        
        for (int i = 0; i < messages->count; i++) {
            Message* msg = messages->messages[i];
            if (!msg->content) continue;
            
            cJSON* m = cJSON_CreateObject();
            switch (msg->role) {
                case ROLE_USER: cJSON_AddStringToObject(m, "role", "user"); break;
                case ROLE_ASSISTANT: cJSON_AddStringToObject(m, "role", "assistant"); break;
                case ROLE_SYSTEM: cJSON_AddStringToObject(m, "role", "system"); break;
                case ROLE_TOOL: cJSON_AddStringToObject(m, "role", "tool"); break;
            }
            cJSON_AddStringToObject(m, "content", msg->content);
            
            // Add images if present (Ollama vision models)
            if (msg->attachments && msg->attachment_count > 0) {
                cJSON* images_array = cJSON_CreateArray();
                int image_count = 0;
                for (int j = 0; j < msg->attachment_count; j++) {
                    Attachment* att = msg->attachments[j];
                    if (att->type == ATTACHMENT_IMAGE && att->data) {
                        log_debug("[Ollama] Adding image attachment: filename=%s, base64_len=%zu, mime=%s",
                                  att->filename ? att->filename : "N/A",
                                  strlen(att->data),
                                  att->mime_type ? att->mime_type : "N/A");
                        cJSON_AddItemToArray(images_array, cJSON_CreateString(att->data));
                        image_count++;
                    }
                }
                if (cJSON_GetArraySize(images_array) > 0) {
                    cJSON_AddItemToObject(m, "images", images_array);
                    log_debug("[Ollama] Added %d image(s) to message", image_count);
                } else {
                    cJSON_Delete(images_array);
                    log_warn("[Ollama] Message has attachments but no valid images found (count=%d)", msg->attachment_count);
                }
            }
            
            cJSON_AddItemToArray(msg_array, m);
        }
        
        cJSON_AddItemToObject(root, "messages", msg_array);
    } else {
        // /api/generate 格式: 使用 prompt 字符串
        char* prompt = build_ollama_prompt(messages, system_prompt);
        cJSON_AddStringToObject(root, "prompt", prompt);
        free(prompt);
    }
    
    // Ollama 选项
    cJSON* options = cJSON_CreateObject();
    cJSON_AddNumberToObject(options, "temperature", self->config.temperature);
    if (self->config.max_tokens > 0) {
        cJSON_AddNumberToObject(options, "num_predict", self->config.max_tokens);
    }
    cJSON_AddItemToObject(root, "options", options);
    
    // 非流式模式
    cJSON_AddBoolToObject(root, "stream", false);

    char* payload = cJSON_PrintUnformatted(root);
    
    // Debug log: truncate base64 images to avoid flooding logs
    {
        cJSON* log_root = cJSON_Parse(payload);
        if (log_root) {
            cJSON* msgs = cJSON_GetObjectItem(log_root, "messages");
            if (msgs && cJSON_IsArray(msgs)) {
                for (int i = 0; i < cJSON_GetArraySize(msgs); i++) {
                    cJSON* m = cJSON_GetArrayItem(msgs, i);
                    cJSON* images = cJSON_GetObjectItem(m, "images");
                    if (images && cJSON_IsArray(images)) {
                        for (int j = 0; j < cJSON_GetArraySize(images); j++) {
                            cJSON* img = cJSON_GetArrayItem(images, j);
                            if (img && cJSON_IsString(img) && strlen(img->valuestring) > 80) {
                                char truncated[128];
                                snprintf(truncated, sizeof(truncated), "%.40s...[TRUNCATED,len=%zu]...%.40s",
                                    img->valuestring, strlen(img->valuestring),
                                    img->valuestring + strlen(img->valuestring) - 40);
                                cJSON_ReplaceItemInArray(images, j, cJSON_CreateString(truncated));
                            }
                        }
                    }
                }
            }
            char* log_payload = cJSON_PrintUnformatted(log_root);
            cJSON_Delete(log_root);
            log_debug("[Ollama] Request: %s", log_payload);
            free(log_payload);
        } else {
            log_debug("[Ollama] Request: (unavailable for logging)");
        }
    }
    cJSON_Delete(root);

    log_debug("[Ollama] Payload size: %zu bytes", strlen(payload));

    // 使用 common http client 发送请求
    HttpResponse* http_resp = http_post(url, payload);
    free(payload);

    AIProviderResponse* response = NULL;

    if (http_resp && http_resp->success) {
        log_debug("[Ollama] Response: %s", http_resp->body);
        
        // 解析响应
        cJSON* resp_root = cJSON_Parse(http_resp->body);
        if (resp_root) {
            // Log prompt_eval_count to diagnose vision model issues
            cJSON* eval_count = cJSON_GetObjectItem(resp_root, "prompt_eval_count");
            if (eval_count && cJSON_IsNumber(eval_count)) {
                log_debug("[Ollama] prompt_eval_count: %d", (int)eval_count->valuedouble);
            }
            
            cJSON* response_text = NULL;
            
            // /api/generate 格式: {"response": "..."}
            response_text = cJSON_GetObjectItem(resp_root, "response");
            
            // /api/chat 格式: {"message": {"role": "assistant", "content": "..."}}
            if (!response_text || !cJSON_IsString(response_text)) {
                cJSON* message = cJSON_GetObjectItem(resp_root, "message");
                if (message && cJSON_IsObject(message)) {
                    response_text = cJSON_GetObjectItem(message, "content");
                }
            }
            
            if (response_text && cJSON_IsString(response_text)) {
                response = ai_provider_response_create(response_text->valuestring, true, NULL);
            } else {
                // 检查是否有错误
                cJSON* error = cJSON_GetObjectItem(resp_root, "error");
                if (error && cJSON_IsString(error)) {
                    response = ai_provider_response_create(NULL, false, error->valuestring);
                } else {
                    response = ai_provider_response_create(NULL, false, "Invalid response format");
                }
            }
            cJSON_Delete(resp_root);
        } else {
            response = ai_provider_response_create(NULL, false, "Failed to parse response");
        }
    } else {
        const char* error_msg = (http_resp && http_resp->body) ? http_resp->body : "HTTP request failed";
        log_error("[Ollama] HTTP error: %s", error_msg);
        response = ai_provider_response_create(NULL, false, error_msg);
    }

    // 清理
    if (http_resp) {
        http_response_free(http_resp);
    }

    return response;
}

AIProvider* provider_ollama_create(const AIProviderConfig* config) {
    AIProvider* provider = (AIProvider*)calloc(1, sizeof(AIProvider));
    provider->name = "ollama";
    
    if (config) {
        provider->config.api_key = config->api_key ? strdup(config->api_key) : NULL;
        provider->config.model_name = config->model_name ? strdup(config->model_name) : strdup("llama3");
        provider->config.base_url = config->base_url ? strdup(config->base_url) : 
                                    strdup("http://localhost:11434");
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
