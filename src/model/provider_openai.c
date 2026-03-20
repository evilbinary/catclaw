#include "ai_provider.h"
#include "ai_provider_factory.h"
#include "common/cJSON.h"
#include "common/log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef HAVE_CURL
#include <curl/curl.h>

// 流式上下文
#define MAX_STREAMING_TOOL_CALLS 8

typedef struct {
    char** buffer;
    bool streaming;
    char sse_line[4096];
    int sse_line_len;
    char* tool_call_ids[MAX_STREAMING_TOOL_CALLS];
    char* tool_call_names[MAX_STREAMING_TOOL_CALLS];
    char* tool_call_arguments[MAX_STREAMING_TOOL_CALLS];
    int tool_call_count;
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

// CURL 写回调
static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    StreamContext* ctx = (StreamContext*)userp;
    char** buffer = ctx->buffer;
    size_t current_len = *buffer ? strlen(*buffer) : 0;

    char* new_buffer = (char*)realloc(*buffer, current_len + realsize + 1);
    if (!new_buffer) {
        fprintf(stderr, "realloc failed\n");
        return 0;
    }

    memcpy(new_buffer + current_len, contents, realsize);
    new_buffer[current_len + realsize] = '\0';
    *buffer = new_buffer;

    if (ctx->streaming) {
        char* data = (char*)contents;
        for (size_t i = 0; i < realsize; i++) {
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

    return realsize;
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

#endif // HAVE_CURL

// OpenAI Provider 私有数据
typedef struct {
    bool initialized;
#ifdef HAVE_CURL
    // CURL 相关数据
#endif
} OpenAIProviderData;

// ==================== 接口实现 ====================

static bool openai_init(AIProvider* self) {
    OpenAIProviderData* data = (OpenAIProviderData*)calloc(1, sizeof(OpenAIProviderData));
    if (!data) return false;
    
#ifdef HAVE_CURL
    curl_global_init(CURL_GLOBAL_ALL);
#endif
    
    data->initialized = true;
    self->impl = data;
    
    log_info("OpenAI Provider initialized: model=%s", 
             self->config.model_name ? self->config.model_name : "gpt-4");
    return true;
}

static void openai_destroy(AIProvider* self) {
    OpenAIProviderData* data = (OpenAIProviderData*)self->impl;
    if (!data) return;
    
#ifdef HAVE_CURL
    curl_global_cleanup();
#endif
    
    free(data);
    self->impl = NULL;
}

static AIProviderResponse* openai_send_messages(AIProvider* self,
                                                 MessageList* messages,
                                                 const char* system_prompt) {
    if (!self || !self->impl) {
        return ai_provider_response_create(NULL, false, "Provider not initialized");
    }
    
#ifdef HAVE_CURL
    CURL* curl = curl_easy_init();
    if (!curl) {
        return ai_provider_response_create(NULL, false, "Failed to initialize curl");
    }

    // 准备请求
    char* response_buffer = (char*)calloc(1, 1);
    StreamContext stream_ctx;
    memset(&stream_ctx, 0, sizeof(stream_ctx));
    stream_ctx.buffer = &response_buffer;
    stream_ctx.streaming = self->config.stream;
    g_stream_ctx = &stream_ctx;

    // 构建 URL
    const char* url = self->config.base_url ? self->config.base_url :
                      "https://api.openai.com/v1/chat/completions";

    // 构建请求头
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s",
             self->config.api_key ? self->config.api_key : "");
    headers = curl_slist_append(headers, auth_header);

    // 构建请求体
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

    log_debug("OpenAI Request: %s", payload);

    // 设置 CURL 选项
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &stream_ctx);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

    // 执行请求
    CURLcode res = curl_easy_perform(curl);
    g_stream_ctx = NULL;

    AIProviderResponse* response = NULL;

    if (res == CURLE_OK) {
        log_debug("OpenAI Response: %s", response_buffer);

        if (self->config.stream) {
            // 解析流式响应
            char* full_text = (char*)calloc(1, 1);
            char* buf_copy = strdup(response_buffer);
            char* line = buf_copy;
            
            while (line) {
                char* line_end = strchr(line, '\n');
                if (line_end) *line_end = '\0';

                if (strncmp(line, "data: ", 6) == 0 && strcmp(line + 6, "[DONE]") != 0) {
                    cJSON* chunk = cJSON_Parse(line + 6);
                    if (chunk) {
                        cJSON* choices = cJSON_GetObjectItem(chunk, "choices");
                        if (choices && cJSON_IsArray(choices)) {
                            cJSON* choice = cJSON_GetArrayItem(choices, 0);
                            cJSON* delta = cJSON_GetObjectItem(choice, "delta");
                            if (delta) {
                                cJSON* content = cJSON_GetObjectItem(delta, "content");
                                if (content && cJSON_IsString(content)) {
                                    size_t old_len = strlen(full_text);
                                    size_t add_len = strlen(content->valuestring);
                                    char* tmp = (char*)realloc(full_text, old_len + add_len + 1);
                                    if (tmp) {
                                        full_text = tmp;
                                        memcpy(full_text + old_len, content->valuestring, add_len + 1);
                                    }
                                }
                            }
                        }
                        cJSON_Delete(chunk);
                    }
                }

                if (line_end) { *line_end = '\n'; line = line_end + 1; }
                else break;
            }
            free(buf_copy);

            response = ai_provider_response_create(full_text, true, NULL);
            
            // 添加 tool_calls
            char* tool_calls_json = build_tool_calls_json(&stream_ctx);
            if (tool_calls_json) {
                response->tool_calls = tool_calls_json;
            }
            
            free(full_text);
        } else {
            // 解析非流式响应
            cJSON* root = cJSON_Parse(response_buffer);
            if (root) {
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
                cJSON_Delete(root);
            }
        }

        if (!response) {
            response = ai_provider_response_create(NULL, false, "Failed to parse response");
        }
    } else {
        response = ai_provider_response_create(NULL, false, curl_easy_strerror(res));
    }

    // 清理
    reset_tool_calls(&stream_ctx);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(payload);
    free(response_buffer);

    return response;
#else
    // Mock 响应
    char mock[256];
    snprintf(mock, sizeof(mock), "[Mock OpenAI] Response for %d messages", 
             messages ? messages->count : 0);
    return ai_provider_response_create(mock, true, NULL);
#endif
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
        provider->config.base_url = config->base_url ? strdup(config->base_url) : 
                                    strdup("https://api.openai.com/v1/chat/completions");
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
