#include "ai_model.h"
#include "common/cJSON.h"
#include "common/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declaration for gateway broadcast
extern bool gateway_broadcast_to_webchat(const char *message);

// Check if curl is available
#ifdef HAVE_CURL
#include <curl/curl.h>

// Streaming callback context
#define MAX_STREAMING_TOOL_CALLS 8

typedef struct {
    char **buffer;
    bool streaming;
    char sse_line[4096];  // Buffer for incomplete SSE lines
    int sse_line_len;
    // Tool calls accumulation for streaming
    char *tool_call_ids[MAX_STREAMING_TOOL_CALLS];       // tool call IDs
    char *tool_call_names[MAX_STREAMING_TOOL_CALLS];     // function names
    char *tool_call_arguments[MAX_STREAMING_TOOL_CALLS]; // accumulated arguments (JSON string)
    int tool_call_count;
} StreamContext;

// Global stream context for tool_calls accumulation (used by SSE processing)
static StreamContext *g_stream_ctx = NULL;

// Forward declarations
static void process_sse_line(const char *line);
static void process_openai_sse_chunk(const char *data);
static void reset_tool_calls(StreamContext *ctx);
static void accumulate_tool_calls(StreamContext *ctx, cJSON *delta);
static char *build_tool_calls_json(StreamContext *ctx);

// Write callback function for curl with streaming support
static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    StreamContext *ctx = (StreamContext *)userp;
    char **buffer = ctx->buffer;
    size_t current_len = *buffer ? strlen(*buffer) : 0;

    // Reallocate buffer to fit new data
    char *new_buffer = (char *)realloc(*buffer, current_len + realsize + 1);
    if (new_buffer == NULL) {
        fprintf(stderr, "realloc failed\n");
        return 0;
    }

    // Copy new data to buffer
    memcpy(new_buffer + current_len, contents, realsize);
    new_buffer[current_len + realsize] = '\0';

    // Update buffer pointer
    *buffer = new_buffer;

    // Process streaming data - handle SSE (Server-Sent Events) format
    if (ctx->streaming) {
        char *data = (char *)contents;
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

// Process a single SSE line
static void process_sse_line(const char *line) {
    // SSE format: "data: {...}"
    if (strncmp(line, "data: ", 6) == 0) {
        const char *json_data = line + 6;
        // Check for stream end
        if (strcmp(json_data, "[DONE]") == 0) {
            log_debug("[SSE] Stream completed");
            return;
        }
        process_openai_sse_chunk(json_data);
    }
}

// Process an OpenAI SSE chunk
static void process_openai_sse_chunk(const char *data) {
    cJSON *root = cJSON_Parse(data);
    if (!root) return;

    cJSON *choices = cJSON_GetObjectItem(root, "choices");
    if (choices && cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
        cJSON *choice = cJSON_GetArrayItem(choices, 0);
        cJSON *delta = cJSON_GetObjectItem(choice, "delta");
        if (delta) {
            // Accumulate tool_calls from streaming chunks
            if (g_stream_ctx) {
                accumulate_tool_calls(g_stream_ctx, delta);
            }

            cJSON *content = cJSON_GetObjectItem(delta, "content");
            if (content && cJSON_IsString(content) && strlen(content->valuestring) > 0) {
                // Print to console for local viewing
                printf("%s", content->valuestring);
                fflush(stdout);
                // Send to WebSocket clients
                gateway_broadcast_to_webchat(content->valuestring);
            }
        }
        // Check for finish_reason
        cJSON *finish = cJSON_GetObjectItem(choice, "finish_reason");
        if (finish && cJSON_IsString(finish) && strcmp(finish->valuestring, "stop") == 0) {
            log_debug("[SSE] Stream finished with stop reason");
        }
    }

    cJSON_Delete(root);
}

// Reset tool_calls accumulation state
static void reset_tool_calls(StreamContext *ctx) {
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

// Accumulate tool_calls from a streaming delta chunk
static void accumulate_tool_calls(StreamContext *ctx, cJSON *delta) {
    cJSON *tool_calls = cJSON_GetObjectItem(delta, "tool_calls");
    if (!tool_calls || !cJSON_IsArray(tool_calls)) return;

    int arr_size = cJSON_GetArraySize(tool_calls);
    for (int i = 0; i < arr_size; i++) {
        cJSON *tc = cJSON_GetArrayItem(tool_calls, i);
        if (!tc) continue;

        // Get the index field to know which tool_call this chunk belongs to
        cJSON *index_obj = cJSON_GetObjectItem(tc, "index");
        int idx = index_obj ? index_obj->valueint : 0;

        if (idx < 0 || idx >= MAX_STREAMING_TOOL_CALLS) continue;

        // Ensure we have enough slots
        if (idx >= ctx->tool_call_count) {
            ctx->tool_call_count = idx + 1;
        }

        // Extract id (usually in first chunk only)
        cJSON *id_obj = cJSON_GetObjectItem(tc, "id");
        if (id_obj && cJSON_IsString(id_obj) && strlen(id_obj->valuestring) > 0) {
            free(ctx->tool_call_ids[idx]);
            ctx->tool_call_ids[idx] = strdup(id_obj->valuestring);
        }

        // Extract type
        // cJSON *type_obj = cJSON_GetObjectItem(tc, "type");

        // Extract function name and arguments
        cJSON *func = cJSON_GetObjectItem(tc, "function");
        if (func) {
            cJSON *name_obj = cJSON_GetObjectItem(func, "name");
            if (name_obj && cJSON_IsString(name_obj) && strlen(name_obj->valuestring) > 0) {
                free(ctx->tool_call_names[idx]);
                ctx->tool_call_names[idx] = strdup(name_obj->valuestring);
            }

            cJSON *args_obj = cJSON_GetObjectItem(func, "arguments");
            if (args_obj && cJSON_IsString(args_obj) && strlen(args_obj->valuestring) > 0) {
                // Append arguments
                size_t old_len = ctx->tool_call_arguments[idx] ? strlen(ctx->tool_call_arguments[idx]) : 0;
                size_t add_len = strlen(args_obj->valuestring);
                char *new_args = (char *)realloc(ctx->tool_call_arguments[idx], old_len + add_len + 1);
                if (new_args) {
                    memcpy(new_args + old_len, args_obj->valuestring, add_len + 1);
                    ctx->tool_call_arguments[idx] = new_args;
                }
            }
        }
    }
}

// Build final tool_calls JSON from accumulated state
// Returns a cJSON array string like: [{"id":"...","type":"function","function":{"name":"...","arguments":"..."}}]
static char *build_tool_calls_json(StreamContext *ctx) {
    if (ctx->tool_call_count == 0) return NULL;

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < ctx->tool_call_count; i++) {
        if (!ctx->tool_call_ids[i] && !ctx->tool_call_names[i]) continue;

        cJSON *tc = cJSON_CreateObject();
        if (ctx->tool_call_ids[i]) {
            cJSON_AddStringToObject(tc, "id", ctx->tool_call_ids[i]);
        }
        cJSON_AddStringToObject(tc, "type", "function");

        cJSON *func = cJSON_CreateObject();
        if (ctx->tool_call_names[i]) {
            cJSON_AddStringToObject(func, "name", ctx->tool_call_names[i]);
        }
        // arguments may be empty for tool_calls with no parameters
        cJSON_AddStringToObject(func, "arguments",
            ctx->tool_call_arguments[i] ? ctx->tool_call_arguments[i] : "");
        cJSON_AddItemToObject(tc, "function", func);

        cJSON_AddItemToArray(arr, tc);
    }

    char *result = cJSON_Print(arr);
    cJSON_Delete(arr);
    return result;
}

#else
// Mock curl functions for testing
#define CURLE_OK 0
#define CURLE_ERROR 1

typedef void CURL;
typedef int CURLcode;
typedef size_t (*curl_write_callback)(void*, size_t, size_t, void*);
typedef struct curl_slist curl_slist;

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    char **buffer = (char **)userp;

    *buffer = (char *)realloc(*buffer, strlen(*buffer) + realsize + 1);
    if (*buffer == NULL) {
        fprintf(stderr, "realloc failed\n");
        return 0;
    }

    strcat(*buffer, (char *)contents);
    return realsize;
}

static CURL *curl_easy_init(void) {
    return (CURL *)1;
}

static void curl_easy_cleanup(CURL *curl __attribute__((unused))) {
}

static CURLcode curl_easy_setopt(CURL *curl __attribute__((unused)), int option __attribute__((unused)), ...) {
    return CURLE_OK;
}

static CURLcode curl_easy_perform(CURL *curl __attribute__((unused))) {
    return CURLE_OK;
}

static curl_slist *curl_slist_append(curl_slist *list __attribute__((unused)), const char *string __attribute__((unused))) {
    return NULL;
}

static void curl_slist_free_all(curl_slist *list __attribute__((unused))) {
}

static void curl_global_init(int flags __attribute__((unused))) {
}

static void curl_global_cleanup(void) {
}

#define CURLOPT_URL 1
#define CURLOPT_HTTPHEADER 2
#define CURLOPT_POST 3
#define CURLOPT_POSTFIELDS 4
#define CURLOPT_WRITEFUNCTION 5
#define CURLOPT_WRITEDATA 6
#define CURLOPT_SSL_VERIFYPEER 7
#endif

// Global AI model configuration
static AIModelConfig g_model_config;
static bool g_initialized = false;



bool ai_model_init(const AIModelConfig *config) {
    if (g_initialized) {
        return true;
    }
    
    // Validate config pointer
    if (!config) {
        fprintf(stderr, "ai_model_init: config is NULL\n");
        return false;
    }

#ifndef NO_CURL
    // Initialize CURL
    if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK) {
        fprintf(stderr, "Failed to initialize curl\n");
        return false;
    }
#else
    // Mock curl initialization
    printf("Mock curl initialized\n");
#endif

    // Initialize all fields to safe defaults first
    g_model_config.type = AI_MODEL_LLAMA;
    g_model_config.api_key = NULL;
    g_model_config.model_name = NULL;
    g_model_config.base_url = NULL;
    g_model_config.temperature = 0.7f;
    g_model_config.max_tokens = 1024;

    // Copy configuration with validation
    g_model_config.type = config->type;
    
    // Safely copy api_key
    if (config->api_key && strlen(config->api_key) > 0) {
        g_model_config.api_key = strdup(config->api_key);
    } else {
        g_model_config.api_key = NULL;
    }
    
    // Safely copy model_name
    if (config->model_name && strlen(config->model_name) > 0) {
        g_model_config.model_name = strdup(config->model_name);
    } else {
        g_model_config.model_name = strdup("llama3.2");  // Default model
    }
    
    // Safely copy base_url
    if (config->base_url && strlen(config->base_url) > 0) {
        g_model_config.base_url = strdup(config->base_url);
    } else {
        g_model_config.base_url = strdup("http://localhost:11434/api/generate");  // Default URL
    }
    
    g_model_config.temperature = config->temperature > 0 ? config->temperature : 0.7f;
    g_model_config.max_tokens = config->max_tokens > 0 ? config->max_tokens : 1024;
    g_model_config.stream = config->stream;

    g_initialized = true;
    printf("AI model initialized: type=%d, model=%s, url=%s\n", 
           g_model_config.type, 
           g_model_config.model_name ? g_model_config.model_name : "(null)",
           g_model_config.base_url ? g_model_config.base_url : "(null)");
    return true;
}

void ai_model_cleanup(void) {
    if (!g_initialized) {
        return;
    }

    // Free configuration
    if (g_model_config.api_key) {
        free(g_model_config.api_key);
        g_model_config.api_key = NULL;
    }
    if (g_model_config.model_name) {
        free(g_model_config.model_name);
        g_model_config.model_name = NULL;
    }
    if (g_model_config.base_url) {
        free(g_model_config.base_url);
        g_model_config.base_url = NULL;
    }

#ifndef NO_CURL
    // Cleanup CURL
    curl_global_cleanup();
#endif
    g_initialized = false;
    printf("AI model cleaned up\n");
}

bool ai_model_set_config(const AIModelConfig *config) {
    if (!config) {
        fprintf(stderr, "ai_model_set_config: config is NULL\n");
        return false;
    }
    
    if (!g_initialized) {
        return ai_model_init(config);
    }

    // Free old configuration
    if (g_model_config.api_key) {
        free(g_model_config.api_key);
    }
    if (g_model_config.model_name) {
        free(g_model_config.model_name);
    }
    if (g_model_config.base_url) {
        free(g_model_config.base_url);
    }

    // Copy new configuration
    g_model_config.type = config->type;
    g_model_config.api_key = config->api_key ? strdup(config->api_key) : NULL;
    g_model_config.model_name = config->model_name ? strdup(config->model_name) : NULL;
    g_model_config.base_url = config->base_url ? strdup(config->base_url) : NULL;
    g_model_config.temperature = config->temperature > 0 ? config->temperature : 0.7f;
    g_model_config.max_tokens = config->max_tokens > 0 ? config->max_tokens : 1024;
    g_model_config.stream = config->stream;

    printf("AI model configuration updated: type=%d, model=%s, url=%s\n", 
           g_model_config.type,
           g_model_config.model_name ? g_model_config.model_name : "(null)",
           g_model_config.base_url ? g_model_config.base_url : "(null)");
    return true;
}

static AIModelResponse *create_response(const char *content, bool success, const char *error) {
    AIModelResponse *response = (AIModelResponse *)malloc(sizeof(AIModelResponse));
    if (!response) {
        return NULL;
    }

    response->content = content ? strdup(content) : NULL;
    response->success = success;
    response->error = error ? strdup(error) : NULL;
    response->tool_calls = NULL;

    return response;
}

AIModelResponse *ai_model_send_message(const char *message) {
    log_debug("[send_message] Called with message=%s", message ? message : "(null)");
    
    if (!g_initialized) {
        log_error("[send_message] AI model not initialized! Call ai_model_init() first");
        return create_response(NULL, false, "AI model not initialized");
    }

#ifdef NO_CURL
    // Mock response when curl is not available
    printf("Mock AI model response for: %s\n", message);
    char mock_response[1024];
    snprintf(mock_response, sizeof(mock_response), "This is a mock response for your message: %s\n\nNote: curl library is not available, so this is a simulated AI response.", message);
    return create_response(mock_response, true, NULL);
#else
    CURL *curl = curl_easy_init();
    if (!curl) {
        return create_response(NULL, false, "Failed to initialize curl");
    }

    char *response_buffer = (char *)calloc(1, 1);
    if (!response_buffer) {
        curl_easy_cleanup(curl);
        return create_response(NULL, false, "Failed to allocate memory");
    }

    struct curl_slist *headers = NULL;
    char *url = NULL;
    char *payload = NULL;

    printf("[DEBUG] Entering switch, model_type=%d\n", g_model_config.type);

    // Log request details
    log_debug("Sending single message to AI model");
    log_debug("Message: %s", message ? message : "(null)");
    log_debug("Model type: %d", g_model_config.type);
    
    switch (g_model_config.type) {
        case AI_MODEL_OPENAI:
            printf("[DEBUG] AI_MODEL_OPENAI branch\n");
            url = g_model_config.base_url ? g_model_config.base_url : "https://api.openai.com/v1/chat/completions";
            log_debug("OpenAI URL: %s", url);
            headers = curl_slist_append(headers, "Content-Type: application/json");
            {
                char auth_header[256];
                const char* api_key = g_model_config.api_key ? g_model_config.api_key : "";
                printf("[DEBUG] api_key=%p, value=%s\n", (void*)api_key, api_key);
                snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
                headers = curl_slist_append(headers, auth_header);
            }
            {
                cJSON *root = cJSON_CreateObject();
                cJSON_AddStringToObject(root, "model", g_model_config.model_name ? g_model_config.model_name : "gpt-4");
                cJSON *messages = cJSON_CreateArray();
                cJSON *message_obj = cJSON_CreateObject();
                cJSON_AddStringToObject(message_obj, "role", "user");
                cJSON_AddStringToObject(message_obj, "content", message);
                cJSON_AddItemToArray(messages, message_obj);
                cJSON_AddItemToObject(root, "messages", messages);
                cJSON_AddNumberToObject(root, "temperature", g_model_config.temperature);
                cJSON_AddNumberToObject(root, "max_tokens", g_model_config.max_tokens);
                cJSON_AddBoolToObject(root, "stream", g_model_config.stream);
                payload = cJSON_Print(root);
                log_debug("OpenAI Request Payload: %s", payload);
                cJSON_Delete(root);
            }
            break;

        case AI_MODEL_ANTHROPIC:
            printf("[DEBUG] AI_MODEL_ANTHROPIC branch\n");
            url = g_model_config.base_url ? g_model_config.base_url : "https://api.anthropic.com/v1/messages";
            log_debug("Anthropic URL: %s", url);
            headers = curl_slist_append(headers, "Content-Type: application/json");
            {
                char auth_header[256];
                const char* api_key = g_model_config.api_key ? g_model_config.api_key : "";
                printf("[DEBUG] api_key=%p\n", (void*)api_key);
                snprintf(auth_header, sizeof(auth_header), "x-api-key: %s", api_key);
                headers = curl_slist_append(headers, auth_header);
            }
            headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
            {
                cJSON *root = cJSON_CreateObject();
                cJSON_AddStringToObject(root, "model", g_model_config.model_name ? g_model_config.model_name : "claude-3-opus-20240229");
                cJSON *messages = cJSON_CreateArray();
                cJSON *message_obj = cJSON_CreateObject();
                cJSON_AddStringToObject(message_obj, "role", "user");
                cJSON_AddStringToObject(message_obj, "content", message);
                cJSON_AddItemToArray(messages, message_obj);
                cJSON_AddNumberToObject(root, "max_tokens", 1024);
                payload = cJSON_Print(root);
                cJSON_Delete(root);
            }
            break;

        case AI_MODEL_GEMINI:
            printf("[DEBUG] AI_MODEL_GEMINI branch\n");
            url = g_model_config.base_url ? g_model_config.base_url : "https://generativelanguage.googleapis.com/v1/models/gemini-1.5-pro:generateContent";
            headers = curl_slist_append(headers, "Content-Type: application/json");
            {
                char auth_header[256];
                const char* api_key = g_model_config.api_key ? g_model_config.api_key : "";
                printf("[DEBUG] api_key=%p\n", (void*)api_key);
                snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
                headers = curl_slist_append(headers, auth_header);
            }
            {
                cJSON *root = cJSON_CreateObject();
                cJSON *contents = cJSON_CreateArray();
                cJSON *content_obj = cJSON_CreateObject();
                cJSON *parts = cJSON_CreateArray();
                cJSON *part_obj = cJSON_CreateObject();
                cJSON_AddStringToObject(part_obj, "text", message);
                cJSON_AddItemToArray(parts, part_obj);
                cJSON_AddItemToObject(content_obj, "parts", parts);
                cJSON_AddStringToObject(content_obj, "role", "user");
                cJSON_AddItemToArray(contents, content_obj);
                cJSON_AddItemToObject(root, "contents", contents);
                payload = cJSON_Print(root);
                cJSON_Delete(root);
            }
            break;

        case AI_MODEL_LLAMA:
            printf("[DEBUG] AI_MODEL_LLAMA branch\n");
            url = g_model_config.base_url ? g_model_config.base_url : "http://localhost:11434/api/generate";
            headers = curl_slist_append(headers, "Content-Type: application/json; charset=utf-8");
            headers = curl_slist_append(headers, "Accept: application/json; charset=utf-8");
            {
                cJSON *root = cJSON_CreateObject();
                // For Llama, extract just the model name without the provider
                const char* model_name = g_model_config.model_name ? g_model_config.model_name : "llama3";
                // Check if model name contains a slash (like "llama/llama3.2")
                const char* slash_pos = strrchr(model_name, '/');
                if (slash_pos) {
                    model_name = slash_pos + 1;
                }
                cJSON_AddStringToObject(root, "model", model_name);
                cJSON_AddStringToObject(root, "prompt", message);
                cJSON_AddNumberToObject(root, "max_tokens", 1024);
                cJSON_AddBoolToObject(root, "stream", g_model_config.stream);
                payload = cJSON_Print(root);
                log_debug("Llama Request Payload: %s", payload);
                cJSON_Delete(root);
            }
            break;

        default:
            curl_easy_cleanup(curl);
            free(response_buffer);
            return create_response(NULL, false, "Unsupported model type");
    }

    // Set up streaming context
    StreamContext stream_ctx;
    memset(&stream_ctx, 0, sizeof(stream_ctx));
    stream_ctx.buffer = &response_buffer;
    stream_ctx.streaming = (g_model_config.type == AI_MODEL_OPENAI && g_model_config.stream);
    g_stream_ctx = &stream_ctx;

    // Set CURL options
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &stream_ctx);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

    // Perform request
    log_debug("Sending request to %s...", url);
    CURLcode res = curl_easy_perform(curl);
    log_debug("Request completed with CURLcode: %d (%s)", res, curl_easy_strerror(res));
    g_stream_ctx = NULL;  // Clear global reference after request

    // Parse response
    AIModelResponse *response = NULL;
    if (res == CURLE_OK) {
        // Log the full response for debugging
        log_debug("Full response from AI model: %s", response_buffer);
        log_debug("Response length: %zu bytes", strlen(response_buffer));

        // For OpenAI streaming, response_buffer contains SSE data, not JSON.
        // Parse it separately before attempting cJSON_Parse.
        if (g_model_config.type == AI_MODEL_OPENAI && g_model_config.stream) {
            log_debug("Parsing OpenAI streaming response...");
            char *full_text = (char *)calloc(1, 1);
            char *buf_copy = strdup(response_buffer);
            char *line = buf_copy;
            while (line) {
                char *line_end = strchr(line, '\n');
                if (line_end) *line_end = '\0';

                if (strncmp(line, "data: ", 6) == 0 && strcmp(line + 6, "[DONE]") != 0) {
                    cJSON *chunk = cJSON_Parse(line + 6);
                    if (chunk) {
                        cJSON *choices = cJSON_GetObjectItem(chunk, "choices");
                        if (choices && cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
                            cJSON *choice = cJSON_GetArrayItem(choices, 0);
                            cJSON *delta = cJSON_GetObjectItem(choice, "delta");
                            if (delta) {
                                cJSON *content = cJSON_GetObjectItem(delta, "content");
                                if (content && cJSON_IsString(content)) {
                                    size_t old_len = strlen(full_text);
                                    size_t add_len = strlen(content->valuestring);
                                    char *tmp = (char *)realloc(full_text, old_len + add_len + 1);
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

            log_debug("[send_messages] full_text: '%s'", full_text ? full_text : "(null)");
            if (strlen(full_text) > 0) {
                log_debug("OpenAI streamed content length: %zu", strlen(full_text));
                response = create_response(full_text, true, NULL);
            } else {
                response = create_response("", true, NULL);
            }

            // Attach accumulated tool_calls from streaming
            if (response) {
                char *tool_calls_json = build_tool_calls_json(&stream_ctx);
                if (tool_calls_json) {
                    response->tool_calls = tool_calls_json;
                    log_debug("[send_message] Streaming tool_calls: %s", tool_calls_json);
                }
            }
            free(full_text);
        } else {
            cJSON *root = cJSON_Parse(response_buffer);
            if (root) {
                switch (g_model_config.type) {
                    case AI_MODEL_OPENAI:
                        log_debug("Parsing OpenAI response...");
                        {
                            cJSON *choices = cJSON_GetObjectItem(root, "choices");
                            if (choices && cJSON_IsArray(choices)) {
                                cJSON *choice = cJSON_GetArrayItem(choices, 0);
                                if (choice) {
                                    cJSON *message = cJSON_GetObjectItem(choice, "message");
                                    if (message) {
                                        cJSON *content = cJSON_GetObjectItem(message, "content");
                                        if (content && cJSON_IsString(content)) {
                                            log_debug("OpenAI response content: %s", content->valuestring);
                                            response = create_response(content->valuestring, true, NULL);
                                        }
                                        
                                        // Extract tool calls
                                        cJSON *tool_calls = cJSON_GetObjectItem(message, "tool_calls");
                                        if (tool_calls && cJSON_IsArray(tool_calls)) {
                                            char *tool_calls_json = cJSON_Print(tool_calls);
                                            if (response) {
                                                response->tool_calls = tool_calls_json;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        break;

                case AI_MODEL_ANTHROPIC:
                    log_debug("Parsing Anthropic response...");
                    {
                        cJSON *content = cJSON_GetObjectItem(root, "content");
                        if (content && cJSON_IsArray(content)) {
                            cJSON *item = cJSON_GetArrayItem(content, 0);
                            if (item) {
                                cJSON *text = cJSON_GetObjectItem(item, "text");
                                if (text && cJSON_IsString(text)) {
                                    log_debug("Anthropic response text: %s", text->valuestring);
                                    response = create_response(text->valuestring, true, NULL);
                                }
                            }
                        }
                    }
                    break;

                case AI_MODEL_GEMINI:
                    log_debug("Parsing Gemini response...");
                    {
                        cJSON *candidates = cJSON_GetObjectItem(root, "candidates");
                        if (candidates && cJSON_IsArray(candidates)) {
                            cJSON *candidate = cJSON_GetArrayItem(candidates, 0);
                            if (candidate) {
                                cJSON *content = cJSON_GetObjectItem(candidate, "content");
                                if (content) {
                                    cJSON *parts = cJSON_GetObjectItem(content, "parts");
                                    if (parts && cJSON_IsArray(parts)) {
                                        cJSON *part = cJSON_GetArrayItem(parts, 0);
                                    if (part) {
                                        cJSON *text = cJSON_GetObjectItem(part, "text");
                                        if (text && cJSON_IsString(text)) {
                                            log_debug("Gemini response text: %s", text->valuestring);
                                            response = create_response(text->valuestring, true, NULL);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    break;

                case AI_MODEL_LLAMA:
                    log_debug("Parsing Llama response...");
                    {
                        // Check for error first
                        cJSON *error_obj = cJSON_GetObjectItem(root, "error");
                        if (error_obj && cJSON_IsString(error_obj)) {
                            response = create_response(NULL, false, error_obj->valuestring);
                        } else {
                            // Handle streaming response (each line is a JSON object)
                            char *buffer_copy = strdup(response_buffer);
                            log_debug("Parsing streaming Llama response...");
                            if (buffer_copy) {
                                char *line = buffer_copy;
                                char *full_response = NULL;
                                size_t full_response_len = 0;

                                while (line) {
                                    char *line_end = strchr(line, '\n');
                                    if (line_end) {
                                        *line_end = '\0';
                                    }
                                    
                                    cJSON *line_root = cJSON_Parse(line);
                                    if (line_root) {
                                        // Check for error in streaming response
                                        cJSON *line_error = cJSON_GetObjectItem(line_root, "error");
                                        if (line_error && cJSON_IsString(line_error)) {
                                            response = create_response(NULL, false, line_error->valuestring);
                                            cJSON_Delete(line_root);
                                            break;
                                        }
                                        
                                        // Check for response field in streaming format
                                        cJSON *response_obj = cJSON_GetObjectItem(line_root, "response");
                                        if (response_obj && cJSON_IsString(response_obj)) {
                                            // Append to full response
                                            size_t line_len = strlen(response_obj->valuestring);
                                            char *new_response = (char *)realloc(full_response, full_response_len + line_len + 1);
                                            if (new_response) {
                                                if (full_response) {
                                                    strcat(new_response, response_obj->valuestring);
                                                } else {
                                                    strcpy(new_response, response_obj->valuestring);
                                                }
                                                full_response = new_response;
                                                full_response_len += line_len;
                                            }
                                        }
                                        cJSON_Delete(line_root);
                                    }
                                    
                                    if (line_end) {
                                        line = line_end + 1;
                                    } else {
                                        break;
                                    }
                                }

                                if (!response && full_response) {
                                    log_debug("Full Llama streaming response: %s", full_response);
                                    response = create_response(full_response, true, NULL);
                                    free(full_response);
                                }
                                
                                free(buffer_copy);
                        }

                        if (!response) {
                            // Fallback to non-streaming format
                            cJSON *response_obj = cJSON_GetObjectItem(root, "response");
                            if (response_obj && cJSON_IsString(response_obj)) {
                                log_debug("Llama non-streaming response: %s", response_obj->valuestring);
                                response = create_response(response_obj->valuestring, true, NULL);
                            } else {
                                response = create_response(NULL, false, "No response from Llama model");
                            }
                        }
                        }
                    }
                    break;
            }

            if (!response) {
                // Check for error
                cJSON *error = cJSON_GetObjectItem(root, "error");
                if (error) {
                    cJSON *message = cJSON_GetObjectItem(error, "message");
                    if (message && cJSON_IsString(message)) {
                        response = create_response(NULL, false, message->valuestring);
                    } else {
                        response = create_response(NULL, false, "API returned an error");
                    }
                } else {
                    response = create_response(NULL, false, "Failed to parse API response");
                }
            }

            cJSON_Delete(root);
        } else {
            response = create_response(NULL, false, "Failed to parse API response");
        }
        }
    } else {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "CURL error: %s", curl_easy_strerror(res));
        response = create_response(NULL, false, error_msg);
    }

    // Cleanup
    reset_tool_calls(&stream_ctx);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    free(response_buffer);
    if (payload) {
        free(payload);
    }

    // Log final response status
    if (response) {
        log_debug("Final response: success=%s, content=%s, error=%s",
                  response->success ? "true" : "false",
                  response->content ? response->content : "(null)",
                  response->error ? response->error : "(null)");
    } else {
        log_debug("Final response: NULL");
    }

    return response;
#endif
}

AIModelResponse *ai_model_send_messages(MessageList *messages, const char *system_prompt) {
    log_debug("[send_messages] Called with messages=%p, count=%d, system_prompt=%p",
              (void*)messages, messages ? messages->count : 0, (void*)system_prompt);
    
    if (!g_initialized) {
        log_error("[send_messages] AI model not initialized! Call ai_model_init() first");
        return create_response(NULL, false, "AI model not initialized");
    }
    
    // Debug logging
    log_debug("ai_model_send_messages: type=%d, model=%s, url=%s\n",
           g_model_config.type,
           g_model_config.model_name ? g_model_config.model_name : "(null)",
           g_model_config.base_url ? g_model_config.base_url : "(null)");
    log_debug("ai_model_send_messages: messages=%p, count=%d, system_prompt=%p\n",
           (void*)messages, messages ? messages->count : 0, (void*)system_prompt);

#ifdef NO_CURL
    // Mock response when curl is not available
    printf("Mock AI model response with %d messages\n", messages ? messages->count : 0);
    char mock_response[1024];
    snprintf(mock_response, sizeof(mock_response), "This is a mock response. Note: curl library is not available.");
    return create_response(mock_response, true, NULL);
#else
    CURL *curl = curl_easy_init();
    if (!curl) {
        return create_response(NULL, false, "Failed to initialize curl");
    }

    char *response_buffer = (char *)calloc(1, 1);
    if (!response_buffer) {
        curl_easy_cleanup(curl);
        return create_response(NULL, false, "Failed to allocate memory");
    }

    struct curl_slist *headers = NULL;
    char *url = NULL;
    char *payload = NULL;

    switch (g_model_config.type) {
        case AI_MODEL_OPENAI:
            url = g_model_config.base_url ? g_model_config.base_url : "https://api.openai.com/v1/chat/completions";
            log_debug("[send_messages] OpenAI URL: %s", url);
            headers = curl_slist_append(headers, "Content-Type: application/json");
            {
                char auth_header[256];
                snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", g_model_config.api_key);
                headers = curl_slist_append(headers, auth_header);
            }
            {
                cJSON *root = cJSON_CreateObject();
                cJSON_AddStringToObject(root, "model", g_model_config.model_name ? g_model_config.model_name : "gpt-4");
                cJSON *messages_json = cJSON_CreateArray();
                
                // Add system prompt
                if (system_prompt && strlen(system_prompt) > 0) {
                    cJSON *system_msg = cJSON_CreateObject();
                    cJSON_AddStringToObject(system_msg, "role", "system");
                    cJSON_AddStringToObject(system_msg, "content", system_prompt);
                    cJSON_AddItemToArray(messages_json, system_msg);
                }
                
                // Add conversation history
                if (messages) {
                    for (int i = 0; i < messages->count; i++) {
                        Message *msg = messages->messages[i];
                        if (!msg) continue;

                        cJSON *msg_obj = cJSON_CreateObject();
                        const char *role_str = "user";
                        if (msg->role == ROLE_ASSISTANT) {
                            role_str = "assistant";
                        } else if (msg->role == ROLE_SYSTEM) {
                            role_str = "system";
                        } else if (msg->role == ROLE_TOOL) {
                            role_str = "tool";
                        }
                        cJSON_AddStringToObject(msg_obj, "role", role_str);

                        // For tool messages, include tool_call_id (required by OpenAI API)
                        if (msg->role == ROLE_TOOL && msg->tool_call_id) {
                            cJSON_AddStringToObject(msg_obj, "tool_call_id", msg->tool_call_id);
                        }

                        // Content can be NULL for assistant messages with only tool_calls
                        const char *content_str = msg->content ? msg->content : "";
                        cJSON_AddStringToObject(msg_obj, "content", content_str);

                        cJSON_AddItemToArray(messages_json, msg_obj);
                    }
                }
                
                cJSON_AddItemToObject(root, "messages", messages_json);
                cJSON_AddNumberToObject(root, "temperature", g_model_config.temperature);
                cJSON_AddNumberToObject(root, "max_tokens", g_model_config.max_tokens);
                cJSON_AddBoolToObject(root, "stream", g_model_config.stream);
                payload = cJSON_Print(root);
                log_debug("[send_messages] OpenAI Request Payload: %s", payload);
                cJSON_Delete(root);
            }
            break;

        case AI_MODEL_ANTHROPIC:
            url = g_model_config.base_url ? g_model_config.base_url : "https://api.anthropic.com/v1/messages";
            log_debug("[send_messages] Anthropic URL: %s", url);
            headers = curl_slist_append(headers, "Content-Type: application/json");
            {
                char auth_header[256];
                const char* api_key = g_model_config.api_key ? g_model_config.api_key : "";
                snprintf(auth_header, sizeof(auth_header), "x-api-key: %s", api_key);
                headers = curl_slist_append(headers, auth_header);
            }
            headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
            {
                cJSON *root = cJSON_CreateObject();
                cJSON_AddStringToObject(root, "model", g_model_config.model_name ? g_model_config.model_name : "claude-3-opus-2024-0229");
                cJSON *messages_json = cJSON_CreateArray();
                
                // Add system prompt (Anthropic uses a separate system field)
                if (system_prompt && strlen(system_prompt) > 0) {
                    cJSON_AddStringToObject(root, "system", system_prompt);
                }
                
                // Add conversation history
                if (messages) {
                    for (int i = 0; i < messages->count; i++) {
                        if (messages->messages[i] && messages->messages[i]->content) {
                            cJSON *msg_obj = cJSON_CreateObject();
                            const char *role_str = "user";
                            if (messages->messages[i]->role == ROLE_ASSISTANT) {
                                role_str = "assistant";
                            }
                            cJSON_AddStringToObject(msg_obj, "role", role_str);
                            cJSON_AddStringToObject(msg_obj, "content", messages->messages[i]->content);
                            cJSON_AddItemToArray(messages_json, msg_obj);
                        }
                    }
                }

                cJSON_AddItemToObject(root, "messages", messages_json);
                cJSON_AddNumberToObject(root, "max_tokens", g_model_config.max_tokens);
                payload = cJSON_Print(root);
                log_debug("[send_messages] Anthropic Request Payload: %s", payload);
                cJSON_Delete(root);
            }
            break;

        case AI_MODEL_GEMINI:
            url = g_model_config.base_url ? g_model_config.base_url : "https://generativelanguage.googleapis.com/v1/models/gemini-1.5-pro:generateContent";
            log_debug("[send_messages] Gemini URL: %s", url);
            headers = curl_slist_append(headers, "Content-Type: application/json");
            {
                char auth_header[256];
                const char* api_key = g_model_config.api_key ? g_model_config.api_key : "";
                snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
                headers = curl_slist_append(headers, auth_header);
            }
            {
                cJSON *root = cJSON_CreateObject();
                cJSON *contents_json = cJSON_CreateArray();
                
                // Add system prompt
                if (system_prompt && strlen(system_prompt) > 0) {
                    cJSON *system_content = cJSON_CreateObject();
                    cJSON *parts = cJSON_CreateArray();
                    cJSON *part = cJSON_CreateObject();
                    cJSON_AddStringToObject(part, "text", system_prompt);
                    cJSON_AddItemToArray(parts, part);
                    cJSON_AddItemToObject(system_content, "parts", parts);
                    cJSON_AddStringToObject(system_content, "role", "user");
                    cJSON_AddItemToArray(contents_json, system_content);
                }
                
                // Add conversation history
                if (messages) {
                    for (int i = 0; i < messages->count; i++) {
                        if (messages->messages[i] && messages->messages[i]->content) {
                            cJSON *content_obj = cJSON_CreateObject();
                            cJSON *parts = cJSON_CreateArray();
                            cJSON *part = cJSON_CreateObject();
                            cJSON_AddStringToObject(part, "text", messages->messages[i]->content);
                            cJSON_AddItemToArray(parts, part);
                            cJSON_AddItemToObject(content_obj, "parts", parts);
                            const char *role_str = "user";
                            if (messages->messages[i]->role == ROLE_ASSISTANT) {
                                role_str = "model";
                            }
                            cJSON_AddStringToObject(content_obj, "role", role_str);
                            cJSON_AddItemToArray(contents_json, content_obj);
                        }
                    }
                }
                
                cJSON_AddItemToObject(root, "contents", contents_json);
                payload = cJSON_Print(root);
                log_debug("[send_messages] Gemini Request Payload: %s", payload);
                cJSON_Delete(root);
            }
            break;

        case AI_MODEL_LLAMA:
            printf("[DEBUG] AI_MODEL_LLAMA branch (send_messages)\n");
            url = g_model_config.base_url ? g_model_config.base_url : "http://localhost:11434/api/generate";
            log_debug("[send_messages] Llama URL: %s", url);
            headers = curl_slist_append(headers, "Content-Type: application/json; charset=utf-8");
            headers = curl_slist_append(headers, "Accept: application/json; charset=utf-8");
            {
                cJSON *root = cJSON_CreateObject();
                const char* model_name = g_model_config.model_name ? g_model_config.model_name : "llama3";
                printf("[DEBUG] model_name=%s\n", model_name);
                const char* slash_pos = strrchr(model_name, '/');
                if (slash_pos) {
                    model_name = slash_pos + 1;
                }
                cJSON_AddStringToObject(root, "model", model_name);
                
                // Build full conversation history for Llama
                char prompt[4096] = "";
                printf("[DEBUG] system_prompt=%p\n", (void*)system_prompt);
                if (system_prompt && strlen(system_prompt) > 0) {
                    snprintf(prompt, sizeof(prompt), "[INST] %s [/INST]\n\n", system_prompt);
                }
                
                log_debug("Building prompt with %d messages in history", messages ? messages->count : 0);
                
                if (messages) {
                    for (int i = 0; i < messages->count; i++) {
                        if (messages->messages[i] && messages->messages[i]->content) {
                            log_debug("Message %d: role=%d, content=%s", i, messages->messages[i]->role, 
                                     messages->messages[i]->content);
                            if (messages->messages[i]->role == ROLE_USER) {
                                strncat(prompt, "[INST] ", sizeof(prompt) - strlen(prompt) - 1);
                                strncat(prompt, messages->messages[i]->content, sizeof(prompt) - strlen(prompt) - 1);
                                strncat(prompt, " [/INST]\n", sizeof(prompt) - strlen(prompt) - 1);
                            } else if (messages->messages[i]->role == ROLE_ASSISTANT) {
                                strncat(prompt, messages->messages[i]->content, sizeof(prompt) - strlen(prompt) - 1);
                                strncat(prompt, "\n\n", sizeof(prompt) - strlen(prompt) - 1);
                            } else if (messages->messages[i]->role == ROLE_TOOL) {
                                // Add tool result to prompt
                                strncat(prompt, "[TOOL_RESULT] ", sizeof(prompt) - strlen(prompt) - 1);
                                strncat(prompt, messages->messages[i]->content, sizeof(prompt) - strlen(prompt) - 1);
                                strncat(prompt, " [/TOOL_RESULT]\n", sizeof(prompt) - strlen(prompt) - 1);
                            }
                        }
                    }
                }
                
                log_debug("Final prompt: %s", prompt);
                
                cJSON_AddStringToObject(root, "prompt", prompt);
                cJSON_AddNumberToObject(root, "max_tokens", g_model_config.max_tokens);
                cJSON_AddBoolToObject(root, "stream", g_model_config.stream);
                payload = cJSON_Print(root);
                log_debug("[send_messages] Llama Request Payload: %s", payload);
                cJSON_Delete(root);
            }
            break;

        default:
            curl_easy_cleanup(curl);
            free(response_buffer);
            return create_response(NULL, false, "Unsupported model type");
    }

    // Set up streaming context
    StreamContext stream_ctx;
    memset(&stream_ctx, 0, sizeof(stream_ctx));
    stream_ctx.buffer = &response_buffer;
    stream_ctx.streaming = (g_model_config.type == AI_MODEL_OPENAI && g_model_config.stream);
    g_stream_ctx = &stream_ctx;

    // Set CURL options
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &stream_ctx);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

    // Perform request
    log_debug("Sending request to %s...", url);
    CURLcode res = curl_easy_perform(curl);
    log_debug("Request completed with CURLcode: %d (%s)", res, curl_easy_strerror(res));
    g_stream_ctx = NULL;  // Clear global reference after request

    // Parse response
    AIModelResponse *response = NULL;
    if (res == CURLE_OK) {
        log_debug("[send_messages] Full response from AI model: %s", response_buffer);
        log_debug("[send_messages] Response length: %zu bytes", strlen(response_buffer));

        // For OpenAI streaming, response_buffer contains SSE data, not JSON.
        // Parse it separately before attempting cJSON_Parse.
        if (g_model_config.type == AI_MODEL_OPENAI && g_model_config.stream) {
            log_debug("[send_messages] Parsing OpenAI streaming response...");
            char *full_text = (char *)calloc(1, 1);
            char *buf_copy = strdup(response_buffer);
            char *line = buf_copy;
            while (line) {
                char *line_end = strchr(line, '\n');
                if (line_end) *line_end = '\0';

                if (strncmp(line, "data: ", 6) == 0 && strcmp(line + 6, "[DONE]") != 0) {
                    cJSON *chunk = cJSON_Parse(line + 6);
                    if (chunk) {
                        cJSON *choices = cJSON_GetObjectItem(chunk, "choices");
                        if (choices && cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
                            cJSON *choice = cJSON_GetArrayItem(choices, 0);
                            cJSON *delta = cJSON_GetObjectItem(choice, "delta");
                            if (delta) {
                                cJSON *content = cJSON_GetObjectItem(delta, "content");
                                if (content && cJSON_IsString(content)) {
                                    size_t old_len = strlen(full_text);
                                    size_t add_len = strlen(content->valuestring);
                                    char *tmp = (char *)realloc(full_text, old_len + add_len + 1);
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

            log_debug("[send_messages] full_text: '%s'", full_text ? full_text : "(null)");
            if (strlen(full_text) > 0) {
                log_debug("[send_messages] OpenAI streamed content length: %zu", strlen(full_text));
                response = create_response(full_text, true, NULL);
            } else {
                response = create_response("", true, NULL);
            }

            // Attach accumulated tool_calls from streaming
            if (response) {
                char *tool_calls_json = build_tool_calls_json(&stream_ctx);
                if (tool_calls_json) {
                    response->tool_calls = tool_calls_json;
                    log_debug("[send_messages] Streaming tool_calls: %s", tool_calls_json);
                }
            }
            free(full_text);
        } else {
            cJSON *root = cJSON_Parse(response_buffer);
            if (root) {
                switch (g_model_config.type) {
                    case AI_MODEL_OPENAI:
                        log_debug("[send_messages] Parsing OpenAI non-streaming response...");
                        {
                            cJSON *choices = cJSON_GetObjectItem(root, "choices");
                            if (choices && cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
                                cJSON *choice = cJSON_GetArrayItem(choices, 0);
                                cJSON *message = cJSON_GetObjectItem(choice, "message");
                                if (message) {
                                    cJSON *content = cJSON_GetObjectItem(message, "content");
                                    if (content && cJSON_IsString(content)) {
                                        log_debug("[send_messages] OpenAI content: %s", content->valuestring);
                                        response = create_response(content->valuestring, true, NULL);
                                    } else {
                                        response = create_response(NULL, false, "No content in OpenAI response");
                                    }
                                } else {
                                    response = create_response(NULL, false, "No message in OpenAI response");
                                }
                            } else {
                                response = create_response(NULL, false, "No choices in OpenAI response");
                            }
                        }
                        break;

                case AI_MODEL_ANTHROPIC:
                    log_debug("Parsing Anthropic response...");
                    {
                        cJSON *content = cJSON_GetObjectItem(root, "content");
                        if (content && cJSON_IsArray(content)) {
                            cJSON *item = cJSON_GetArrayItem(content, 0);
                            if (item) {
                                cJSON *text = cJSON_GetObjectItem(item, "text");
                                if (text && cJSON_IsString(text)) {
                                    log_debug("Anthropic response text: %s", text->valuestring);
                                    response = create_response(text->valuestring, true, NULL);
                                }
                            }
                        }
                    }
                    break;

                case AI_MODEL_GEMINI:
                    log_debug("Parsing Gemini response...");
                    {
                        cJSON *candidates = cJSON_GetObjectItem(root, "candidates");
                        if (candidates && cJSON_IsArray(candidates)) {
                            cJSON *candidate = cJSON_GetArrayItem(candidates, 0);
                            if (candidate) {
                                cJSON *content = cJSON_GetObjectItem(candidate, "content");
                                if (content) {
                                    cJSON *parts = cJSON_GetObjectItem(content, "parts");
                                    if (parts && cJSON_IsArray(parts)) {
                                        cJSON *part = cJSON_GetArrayItem(parts, 0);
                                    if (part) {
                                        cJSON *text = cJSON_GetObjectItem(part, "text");
                                        if (text && cJSON_IsString(text)) {
                                            log_debug("Gemini response text: %s", text->valuestring);
                                            response = create_response(text->valuestring, true, NULL);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    break;

                case AI_MODEL_LLAMA:
                    log_debug("Parsing Llama response...");
                    {
                        // Check for error first
                        cJSON *error_obj = cJSON_GetObjectItem(root, "error");
                        if (error_obj && cJSON_IsString(error_obj)) {
                            response = create_response(NULL, false, error_obj->valuestring);
                        } else {
                            // Handle /api/generate endpoint response format
                            cJSON *response_obj = cJSON_GetObjectItem(root, "response");
                            if (response_obj && cJSON_IsString(response_obj)) {
                                response = create_response(response_obj->valuestring, true, NULL);
                                
                                // Check for tool_calls in response content
                                if (response && response->content) {
                                    const char* tool_calls_start = strstr(response->content, "\"tool_calls\"");
                                    if (tool_calls_start) {
                                        // Find the start of JSON array
                                        const char* array_start = strchr(tool_calls_start, '[');
                                        if (array_start) {
                                            // Find the matching end of array
                                            int bracket_count = 1;
                                            const char* ptr = array_start + 1;
                                            while (*ptr && bracket_count > 0) {
                                                if (*ptr == '[') bracket_count++;
                                                else if (*ptr == ']') bracket_count--;
                                                ptr++;
                                            }
                                            
                                            if (bracket_count == 0) {
                                                // Extract the JSON array
                                                size_t json_len = ptr - array_start;
                                                char* json_str = (char*)malloc(json_len + 1);
                                                if (json_str) {
                                                    strncpy(json_str, array_start, json_len);
                                                    json_str[json_len] = '\0';
                                                    response->tool_calls = json_str;
                                                }
                                            }
                                        }
                                    }
                                }
                            } else {
                                response = create_response(NULL, false, "No response from Llama model");
                            }
                        }
                    }
                    break;
            }

            if (!response) {
                cJSON *error = cJSON_GetObjectItem(root, "error");
                if (error) {
                    cJSON *message = cJSON_GetObjectItem(error, "message");
                    if (message && cJSON_IsString(message)) {
                        response = create_response(NULL, false, message->valuestring);
                    } else {
                        response = create_response(NULL, false, "API returned an error");
                    }
                } else {
                    response = create_response(NULL, false, "Failed to parse API response");
                }
            }

            cJSON_Delete(root);
        } else {
            response = create_response(NULL, false, "Failed to parse API response");
        }
        }
    } else {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "CURL error: %s", curl_easy_strerror(res));
        response = create_response(NULL, false, error_msg);
    }

    // Cleanup
    reset_tool_calls(&stream_ctx);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    free(response_buffer);
    if (payload) {
        free(payload);
    }

    // Log final response status
    if (response) {
        log_debug("Final response: success=%s, content=%s, error=%s",
                  response->success ? "true" : "false",
                  response->content ? response->content : "(null)",
                  response->error ? response->error : "(null)");
    } else {
        log_debug("Final response: NULL");
    }

    return response;
#endif
}

void ai_model_free_response(AIModelResponse *response) {
    if (response) {
        if (response->content) {
            free(response->content);
        }
        if (response->error) {
            free(response->error);
        }
        if (response->tool_calls) {
            free(response->tool_calls);
        }
        free(response);
    }
}
