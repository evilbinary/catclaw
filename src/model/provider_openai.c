#include "ai_provider.h"
#include "ai_provider_factory.h"
#include "common/cJSON.h"
#include "common/log.h"
#include "common/http_client.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// 流式上下文
#define MAX_STREAMING_TOOL_CALLS 8

typedef struct {
    bool streaming;
    char sse_line[4096];
    int sse_line_len;
    char* tool_call_ids[MAX_STREAMING_TOOL_CALLS];
    char* tool_call_names[MAX_STREAMING_TOOL_CALLS];
    char* tool_call_arguments[MAX_STREAMING_TOOL_CALLS];
    int tool_call_count;
    char* accumulated_content;  // 累积的内容
    AIProvider* provider;       // provider 指针，用于访问流式回调
} StreamContext;

static StreamContext* g_stream_ctx = NULL;

// 前向声明
static void process_sse_line(const char* line);
static void process_openai_sse_chunk(const char* data);
static void reset_tool_calls(StreamContext* ctx);
static void accumulate_tool_calls(StreamContext* ctx, cJSON* delta);
static char* build_tool_calls_json(StreamContext* ctx);

// 外部广播函数
extern bool gateway_broadcast_to_webchat(const char* message);

// 流式数据回调 (供 http_request_stream 使用)
static bool stream_data_callback(const char* data, size_t len, void* userp) {
    StreamContext* ctx = (StreamContext*)userp;

    if (ctx->streaming) {
        for (size_t i = 0; i < len; i++) {
            char ch = data[i];
            if (ch == '\n' || ctx->sse_line_len >= (int)sizeof(ctx->sse_line) - 1) {
                ctx->sse_line[ctx->sse_line_len] = '\0';
                if (ctx->sse_line_len > 0) {
                    process_sse_line(ctx->sse_line);
                }
                ctx->sse_line_len = 0;
            } else if (ch != '\r') {
                ctx->sse_line[ctx->sse_line_len++] = ch;
            }
        }
    }

    return true;  // 继续接收
}

static void process_sse_line(const char* line) {
    if (strncmp(line, "data: ", 6) == 0) {
        const char* json_data = line + 6;
        if (strcmp(json_data, "[DONE]") == 0) {
            log_debug("[SSE] Stream completed");
            return;
        }
        process_openai_sse_chunk(json_data);
    }
}

static void process_openai_sse_chunk(const char* data) {
    cJSON* root = cJSON_Parse(data);
    if (!root) return;

    cJSON* choices = cJSON_GetObjectItem(root, "choices");
    if (choices && cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
        cJSON* choice = cJSON_GetArrayItem(choices, 0);
        cJSON* delta = cJSON_GetObjectItem(choice, "delta");
        if (delta) {
            if (g_stream_ctx) {
                accumulate_tool_calls(g_stream_ctx, delta);
            }
            cJSON* content = cJSON_GetObjectItem(delta, "content");
            if (content && cJSON_IsString(content) && strlen(content->valuestring) > 0) {
                printf("%s", content->valuestring);
                fflush(stdout);
                gateway_broadcast_to_webchat(content->valuestring);
                
                // 累积内容并调用流式回调
                if (g_stream_ctx && g_stream_ctx->accumulated_content) {
                    size_t old_len = strlen(g_stream_ctx->accumulated_content);
                    size_t chunk_len = strlen(content->valuestring);
                    char* new_content = (char*)realloc(g_stream_ctx->accumulated_content, old_len + chunk_len + 1);
                    if (new_content) {
                        strcat(new_content, content->valuestring);
                        g_stream_ctx->accumulated_content = new_content;
                        
                        // 调用流式回调（从 provider config 获取）
                        if (g_stream_ctx->provider && g_stream_ctx->provider->config.stream_callback) {
                            g_stream_ctx->provider->config.stream_callback(
                                content->valuestring, 
                                g_stream_ctx->accumulated_content, 
                                g_stream_ctx->provider->config.stream_user_data);
                        }
                    }
                }
            }
        }
    }

    cJSON_Delete(root);
}

static void reset_tool_calls(StreamContext* ctx) {
    for (int i = 0; i < ctx->tool_call_count; i++) {
        free(ctx->tool_call_ids[i]);
        free(ctx->tool_call_names[i]);
        free(ctx->tool_call_arguments[i]);
        ctx->tool_call_ids[i] = NULL;
        ctx->tool_call_names[i] = NULL;
        ctx->tool_call_arguments[i] = NULL;
    }
    ctx->tool_call_count = 0;
}

static void accumulate_tool_calls(StreamContext* ctx, cJSON* delta) {
    cJSON* tool_calls = cJSON_GetObjectItem(delta, "tool_calls");
    if (!tool_calls || !cJSON_IsArray(tool_calls)) return;

    int arr_size = cJSON_GetArraySize(tool_calls);
    for (int i = 0; i < arr_size; i++) {
        cJSON* tc = cJSON_GetArrayItem(tool_calls, i);
        if (!tc) continue;

        cJSON* index_obj = cJSON_GetObjectItem(tc, "index");
        int idx = index_obj ? index_obj->valueint : 0;

        if (idx < 0 || idx >= MAX_STREAMING_TOOL_CALLS) continue;

        if (idx >= ctx->tool_call_count) {
            ctx->tool_call_count = idx + 1;
        }

        cJSON* id_obj = cJSON_GetObjectItem(tc, "id");
        if (id_obj && cJSON_IsString(id_obj) && strlen(id_obj->valuestring) > 0) {
            free(ctx->tool_call_ids[idx]);
            ctx->tool_call_ids[idx] = strdup(id_obj->valuestring);
        }

        cJSON* func = cJSON_GetObjectItem(tc, "function");
        if (func) {
            cJSON* name_obj = cJSON_GetObjectItem(func, "name");
            if (name_obj && cJSON_IsString(name_obj) && strlen(name_obj->valuestring) > 0) {
                free(ctx->tool_call_names[idx]);
                ctx->tool_call_names[idx] = strdup(name_obj->valuestring);
            }

            cJSON* args_obj = cJSON_GetObjectItem(func, "arguments");
            if (args_obj && cJSON_IsString(args_obj) && strlen(args_obj->valuestring) > 0) {
                size_t old_len = ctx->tool_call_arguments[idx] ? strlen(ctx->tool_call_arguments[idx]) : 0;
                size_t add_len = strlen(args_obj->valuestring);
                char* new_args = (char*)realloc(ctx->tool_call_arguments[idx], old_len + add_len + 1);
                if (new_args) {
                    memcpy(new_args + old_len, args_obj->valuestring, add_len + 1);
                    ctx->tool_call_arguments[idx] = new_args;
                }
            }
        }
    }
}

static char* build_tool_calls_json(StreamContext* ctx) {
    if (ctx->tool_call_count == 0) return NULL;

    cJSON* arr = cJSON_CreateArray();
    for (int i = 0; i < ctx->tool_call_count; i++) {
        if (!ctx->tool_call_ids[i] && !ctx->tool_call_names[i]) continue;

        cJSON* tc = cJSON_CreateObject();
        if (ctx->tool_call_ids[i]) {
            cJSON_AddStringToObject(tc, "id", ctx->tool_call_ids[i]);
        }
        cJSON_AddStringToObject(tc, "type", "function");

        cJSON* func = cJSON_CreateObject();
        if (ctx->tool_call_names[i]) {
            cJSON_AddStringToObject(func, "name", ctx->tool_call_names[i]);
        }
        cJSON_AddStringToObject(func, "arguments",
            ctx->tool_call_arguments[i] ? ctx->tool_call_arguments[i] : "");
        cJSON_AddItemToObject(tc, "function", func);

        cJSON_AddItemToArray(arr, tc);
    }

    char* result = cJSON_Print(arr);
    cJSON_Delete(arr);
    return result;
}

// OpenAI Provider 私有数据
typedef struct {
    bool initialized;
} OpenAIProviderData;

// ==================== 接口实现 ====================

static bool openai_init(AIProvider* self) {
    OpenAIProviderData* data = (OpenAIProviderData*)calloc(1, sizeof(OpenAIProviderData));
    if (!data) return false;
    
    http_client_init();
    
    data->initialized = true;
    self->impl = data;
    
    log_info("OpenAI Provider initialized: model=%s", 
             self->config.model_name ? self->config.model_name : "gpt-4");
    return true;
}

static void openai_destroy(AIProvider* self) {
    OpenAIProviderData* data = (OpenAIProviderData*)self->impl;
    if (!data) return;
    
    http_client_cleanup();
    
    free(data);
    self->impl = NULL;
}

// 构建 API URL: base_url + /v1/chat/completions
static char* openai_build_url(const AIProvider* self) {
    const char* base = self->config.base_url;
    if (!base) {
        return strdup("https://api.openai.com/v1/chat/completions");
    }
    
    size_t base_len = strlen(base);
    // 去掉末尾的 '/'
    while (base_len > 0 && base[base_len - 1] == '/') {
        base_len--;
    }
    
    const char* api_path = "chat/completions";
    size_t path_len = strlen(api_path);
    
    // 检查是否已包含 chat/completions
    if (strstr(base, "/chat/completions")) {
        char* url = (char*)malloc(base_len + 1);
        strncpy(url, base, base_len);
        url[base_len] = '\0';
        return url;
    }
    
    // 检查是否以 /v1 或 /v3 结尾 (用 strncmp 避免原始字符串尾部 '/' 干扰)
    const char* v1_suffix = "v1";
    const char* v3_suffix = "v3";
    size_t v1_len = strlen(v1_suffix);
    size_t v3_len = strlen(v3_suffix);
    if ((base_len >= v1_len && strncmp(base + base_len - v1_len, v1_suffix, v1_len) == 0) ||
        (base_len >= v3_len && strncmp(base + base_len - v3_len, v3_suffix, v3_len) == 0)) {
        char* url = (char*)malloc(base_len + 1 + path_len + 1);
        snprintf(url, base_len + 1 + path_len + 1, "%.*s/%s", (int)base_len, base, api_path);
        return url;
    }
    
    // 检查是否包含 /v1/ 路径
    if (strstr(base, "/v1/")) {
        char* url = (char*)malloc(base_len + 1);
        strncpy(url, base, base_len);
        url[base_len] = '\0';
        return url;
    }
    
    // 检查是否包含 /api/ 路径 (如 Ollama)
    if (strstr(base, "/api/")) {
        char* url = (char*)malloc(base_len + 1);
        strncpy(url, base, base_len);
        url[base_len] = '\0';
        return url;
    }
    
    // 默认: base_url/v1/chat/completions
    char* url = (char*)malloc(base_len + 4 + path_len + 1);
    snprintf(url, base_len + 4 + path_len + 1, "%.*s/v1/%s", (int)base_len, base, api_path);
    return url;
}

// 构建请求体
static char* openai_build_request_body(AIProvider* self, MessageList* messages,
                                        const char* system_prompt) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", 
        self->config.model_name ? self->config.model_name : "gpt-4");
    
    cJSON* msg_arr = cJSON_CreateArray();
    
    // 添加 system prompt
    if (system_prompt && strlen(system_prompt) > 0) {
        cJSON* sys_msg = cJSON_CreateObject();
        cJSON_AddStringToObject(sys_msg, "role", "system");
        cJSON_AddStringToObject(sys_msg, "content", system_prompt);
        cJSON_AddItemToArray(msg_arr, sys_msg);
    }
    
    // 添加消息列表
    if (messages) {
        for (int i = 0; i < messages->count; i++) {
            Message* msg = messages->messages[i];
            cJSON* msg_obj = cJSON_CreateObject();
            const char* role = "user";
            switch (msg->role) {
                case ROLE_USER: role = "user"; break;
                case ROLE_ASSISTANT: role = "assistant"; break;
                case ROLE_SYSTEM: role = "system"; break;
                case ROLE_TOOL: role = "tool"; break;
            }
            cJSON_AddStringToObject(msg_obj, "role", role);
            if (msg->content) {
                cJSON_AddStringToObject(msg_obj, "content", msg->content);
            }
            if (msg->tool_call_id) {
                cJSON_AddStringToObject(msg_obj, "tool_call_id", msg->tool_call_id);
            }
            if (msg->tool_name) {
                cJSON_AddStringToObject(msg_obj, "name", msg->tool_name);
            }
            cJSON_AddItemToArray(msg_arr, msg_obj);
        }
    }
    
    cJSON_AddItemToObject(root, "messages", msg_arr);
    cJSON_AddNumberToObject(root, "temperature", self->config.temperature);
    cJSON_AddNumberToObject(root, "max_tokens", self->config.max_tokens);
    cJSON_AddBoolToObject(root, "stream", self->config.stream);

    char* payload = cJSON_Print(root);
    cJSON_Delete(root);
    return payload;
}

// 构建请求头
static HttpHeaders* openai_build_headers(AIProvider* self) {
    HttpHeaders* headers = http_headers_new();
    
    char auth_value[1024];
    snprintf(auth_value, sizeof(auth_value), "Bearer %s",
             self->config.api_key ? self->config.api_key : "");
    http_headers_add(headers, "Authorization", auth_value);
    
    return headers;
}

// 解析非流式响应
static AIProviderResponse* openai_parse_non_stream_response(HttpResponse* http_resp) {
    if (!http_resp || !http_resp->body) {
        return ai_provider_response_create(NULL, false, 
            http_resp ? "Empty response body" : "Request failed");
    }
    
    cJSON* root = cJSON_Parse(http_resp->body);
    if (!root) {
        return ai_provider_response_create(NULL, false, "Failed to parse response");
    }

    AIProviderResponse* response = NULL;
    cJSON* choices = cJSON_GetObjectItem(root, "choices");
    if (choices && cJSON_IsArray(choices)) {
        cJSON* choice = cJSON_GetArrayItem(choices, 0);
        if (choice) {
            cJSON* msg = cJSON_GetObjectItem(choice, "message");
            if (msg) {
                cJSON* content = cJSON_GetObjectItem(msg, "content");
                response = ai_provider_response_create(
                    content && cJSON_IsString(content) ? content->valuestring : NULL,
                    true, NULL);
                
                // 提取 tool_calls
                cJSON* tool_calls = cJSON_GetObjectItem(msg, "tool_calls");
                if (tool_calls && cJSON_IsArray(tool_calls)) {
                    response->tool_calls = cJSON_Print(tool_calls);
                }
            }
        }
    }
    
    // 检查 API 错误
    if (!response) {
        cJSON* err = cJSON_GetObjectItem(root, "error");
        if (err && cJSON_IsObject(err)) {
            cJSON* msg = cJSON_GetObjectItem(err, "message");
            const char* err_str = msg && cJSON_IsString(msg) ? msg->valuestring : "Unknown API error";
            log_error("[OpenAI] API error: %s", err_str);
            response = ai_provider_response_create(NULL, false, err_str);
        } else {
            response = ai_provider_response_create(NULL, false, "Failed to parse response");
        }
    }
    
    cJSON_Delete(root);
    return response;
}

// 解析流式响应 (从累积内容中提取)
static AIProviderResponse* openai_parse_stream_response(StreamContext* stream_ctx) {
    AIProviderResponse* response = ai_provider_response_create(
        stream_ctx->accumulated_content, true, NULL);
    
    // 添加 tool_calls
    char* tool_calls_json = build_tool_calls_json(stream_ctx);
    if (tool_calls_json) {
        response->tool_calls = tool_calls_json;
    }
    
    return response;
}

static AIProviderResponse* openai_send_messages(AIProvider* self,
                                                 MessageList* messages,
                                                 const char* system_prompt) {
    if (!self || !self->impl) {
        return ai_provider_response_create(NULL, false, "Provider not initialized");
    }
    
    // 构建 URL
    char* url = openai_build_url(self);
    if (!url) {
        return ai_provider_response_create(NULL, false, "Failed to build API URL");
    }
    
    // 构建请求体
    char* payload = openai_build_request_body(self, messages, system_prompt);
    
    // 构建请求头
    HttpHeaders* headers = openai_build_headers(self);
    
    log_debug("[OpenAI] Request URL: %s", url);
    log_debug("[OpenAI] Request payload: %s", payload);

    AIProviderResponse* response = NULL;

    if (self->config.stream) {
        // ===== 流式请求 =====
        StreamContext stream_ctx;
        memset(&stream_ctx, 0, sizeof(stream_ctx));
        stream_ctx.streaming = true;
        stream_ctx.accumulated_content = (char*)calloc(1, 1);
        stream_ctx.provider = self;
        g_stream_ctx = &stream_ctx;

        HttpRequest req = {
            .url = url,
            .method = "POST",
            .body = payload,
            .content_type = "application/json",
            .headers = http_headers_to_array(headers),
            .timeout_sec = 120
        };

        HttpResponse* stream_resp = http_request_stream(&req, stream_data_callback, &stream_ctx);
        free((void*)req.headers);  // http_headers_to_array 返回的需要 free
        g_stream_ctx = NULL;

        if (stream_resp) {
            log_debug("[OpenAI] Stream response: status=%d, success=%s, body=%s",
                stream_resp->status_code,
                stream_resp->success ? "true" : "false",
                stream_resp->body ? stream_resp->body : "(null)");
            if (stream_resp->success) {
                response = openai_parse_stream_response(&stream_ctx);
            } else {
                char err_msg[512];
                snprintf(err_msg, sizeof(err_msg), "Stream request failed: HTTP %d",
                         stream_resp->status_code);
                if (stream_resp->body) {
                    log_error("[OpenAI] Response body: %s", stream_resp->body);
                    cJSON* err_root = cJSON_Parse(stream_resp->body);
                    if (err_root) {
                        cJSON* err_obj = cJSON_GetObjectItem(err_root, "error");
                        if (err_obj && cJSON_IsObject(err_obj)) {
                            cJSON* msg = cJSON_GetObjectItem(err_obj, "message");
                            if (msg && cJSON_IsString(msg)) {
                                snprintf(err_msg, sizeof(err_msg), "Stream request failed: %s",
                                         msg->valuestring);
                            }
                        }
                        cJSON_Delete(err_root);
                    }
                }
                log_error("[OpenAI] %s", err_msg);
                response = ai_provider_response_create(NULL, false, err_msg);
            }
            http_response_free(stream_resp);
        } else {
            log_error("[OpenAI] Stream request failed: no response");
            response = ai_provider_response_create(NULL, false, "Stream request failed: no response");
        }

        reset_tool_calls(&stream_ctx);
        free(stream_ctx.accumulated_content);
    } else {
        // ===== 非流式请求 =====
        HttpResponse* http_resp = http_post_json_with_headers(url, payload, headers);
        
        log_debug("[OpenAI] HTTP status: %d, success: %s",
            http_resp ? http_resp->status_code : 0,
            http_resp ? (http_resp->success ? "true" : "false") : "N/A");
        log_debug("[OpenAI] Response body: %s",
            http_resp && http_resp->body ? http_resp->body : "(null)");

        if (http_resp) {
            response = openai_parse_non_stream_response(http_resp);
            http_response_free(http_resp);
        } else {
            response = ai_provider_response_create(NULL, false, "HTTP request failed");
        }
    }

    http_headers_free(headers);
    free(payload);
    free(url);

    return response;
}

static void openai_free_response(AIProviderResponse* response) {
    ai_provider_response_destroy(response);
}

// ==================== 创建函数 ====================

AIProvider* provider_openai_create(const AIProviderConfig* config) {
    AIProvider* provider = (AIProvider*)calloc(1, sizeof(AIProvider));
    if (!provider) return NULL;
    
    provider->name = "openai";
    
    // 复制配置
    if (config) {
        provider->config.api_key = config->api_key ? strdup(config->api_key) : NULL;
        provider->config.model_name = config->model_name ? strdup(config->model_name) : strdup("gpt-4");
        provider->config.base_url = config->base_url ? strdup(config->base_url) : NULL;
        provider->config.temperature = config->temperature > 0 ? config->temperature : 0.7f;
        provider->config.max_tokens = config->max_tokens > 0 ? config->max_tokens : 4096;
        provider->config.stream = config->stream;
    }
    
    // 设置接口方法
    provider->init = openai_init;
    provider->destroy = openai_destroy;
    provider->send_messages = openai_send_messages;
    provider->free_response = openai_free_response;
    
    // 初始化
    if (!provider->init(provider)) {
        free(provider);
        return NULL;
    }
    
    return provider;
}
