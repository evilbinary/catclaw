#include "ai_model.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Check if curl is available
#ifdef HAVE_CURL
#include <curl/curl.h>
#else
// Mock curl functions for testing
#define CURLE_OK 0
#define CURLE_ERROR 1

typedef void CURL;
typedef int CURLcode;
typedef size_t (*curl_write_callback)(void*, size_t, size_t, void*);
typedef struct curl_slist curl_slist;

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

// Callback function for curl
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

// Global AI model configuration
static AIModelConfig g_model_config;
static bool g_initialized = false;



bool ai_model_init(const AIModelConfig *config) {
    if (g_initialized) {
        return true;
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

    // Copy configuration
    g_model_config.type = config->type;
    g_model_config.api_key = config->api_key ? strdup(config->api_key) : NULL;
    g_model_config.model_name = config->model_name ? strdup(config->model_name) : NULL;
    g_model_config.base_url = config->base_url ? strdup(config->base_url) : NULL;

    g_initialized = true;
    printf("AI model initialized: %s\n", g_model_config.model_name);
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

    printf("AI model configuration updated: %s\n", g_model_config.model_name);
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

    return response;
}

AIModelResponse *ai_model_send_message(const char *message) {
    if (!g_initialized) {
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

    switch (g_model_config.type) {
        case AI_MODEL_OPENAI:
            url = g_model_config.base_url ? g_model_config.base_url : "https://api.openai.com/v1/chat/completions";
            headers = curl_slist_append(headers, "Content-Type: application/json");
            {
                char auth_header[256];
                snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", g_model_config.api_key);
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
                payload = cJSON_Print(root);
                cJSON_Delete(root);
            }
            break;

        case AI_MODEL_ANTHROPIC:
            url = g_model_config.base_url ? g_model_config.base_url : "https://api.anthropic.com/v1/messages";
            headers = curl_slist_append(headers, "Content-Type: application/json");
            {
                char auth_header[256];
                snprintf(auth_header, sizeof(auth_header), "x-api-key: %s", g_model_config.api_key);
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
            url = g_model_config.base_url ? g_model_config.base_url : "https://generativelanguage.googleapis.com/v1/models/gemini-1.5-pro:generateContent";
            headers = curl_slist_append(headers, "Content-Type: application/json");
            {
                char auth_header[256];
                snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", g_model_config.api_key);
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
            url = g_model_config.base_url ? g_model_config.base_url : "http://localhost:11434/api/generate";
            headers = curl_slist_append(headers, "Content-Type: application/json");
            {
                cJSON *root = cJSON_CreateObject();
                cJSON_AddStringToObject(root, "model", g_model_config.model_name ? g_model_config.model_name : "llama3");
                cJSON_AddStringToObject(root, "prompt", message);
                cJSON_AddNumberToObject(root, "max_tokens", 1024);
                cJSON_AddBoolToObject(root, "stream", false);
                payload = cJSON_Print(root);
                cJSON_Delete(root);
            }
            break;

        default:
            curl_easy_cleanup(curl);
            free(response_buffer);
            return create_response(NULL, false, "Unsupported model type");
    }

    // Set CURL options
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_buffer);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); // Disable SSL verification for testing

    // Perform request
    CURLcode res = curl_easy_perform(curl);

    // Parse response
    AIModelResponse *response = NULL;
    if (res == CURLE_OK) {
        cJSON *root = cJSON_Parse(response_buffer);
        if (root) {
            switch (g_model_config.type) {
                case AI_MODEL_OPENAI:
                    {
                        cJSON *choices = cJSON_GetObjectItem(root, "choices");
                        if (choices && cJSON_IsArray(choices)) {
                            cJSON *choice = cJSON_GetArrayItem(choices, 0);
                            if (choice) {
                                cJSON *message = cJSON_GetObjectItem(choice, "message");
                                if (message) {
                                    cJSON *content = cJSON_GetObjectItem(message, "content");
                                    if (content && cJSON_IsString(content)) {
                                        response = create_response(content->valuestring, true, NULL);
                                    }
                                }
                            }
                        }
                    }
                    break;

                case AI_MODEL_ANTHROPIC:
                    {
                        cJSON *content = cJSON_GetObjectItem(root, "content");
                        if (content && cJSON_IsArray(content)) {
                            cJSON *item = cJSON_GetArrayItem(content, 0);
                            if (item) {
                                cJSON *text = cJSON_GetObjectItem(item, "text");
                                if (text && cJSON_IsString(text)) {
                                    response = create_response(text->valuestring, true, NULL);
                                }
                            }
                        }
                    }
                    break;

                case AI_MODEL_GEMINI:
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
                    {
                        cJSON *response_obj = cJSON_GetObjectItem(root, "response");
                        if (response_obj && cJSON_IsString(response_obj)) {
                            response = create_response(response_obj->valuestring, true, NULL);
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
    } else {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "CURL error: %s", curl_easy_strerror(res));
        response = create_response(NULL, false, error_msg);
    }

    // Cleanup
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    free(response_buffer);
    if (payload) {
        free(payload);
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
        free(response);
    }
}
