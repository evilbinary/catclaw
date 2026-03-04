#include "model.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

// Callback function for curl
static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    char** response = (char**)userp;
    
    char* temp = realloc(*response, strlen(*response) + realsize + 1);
    if (!temp) {
        printf("Error: Memory allocation failed\n");
        return 0;
    }
    
    *response = temp;
    memcpy(*response + strlen(*response), contents, realsize);
    (*response)[strlen(*response) + realsize] = '\0';
    
    return realsize;
}

// Build API request JSON
static char* build_api_request(const char* model, MessageList* context) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", model);
    
    cJSON* messages = cJSON_CreateArray();
    for (int i = 0; i < context->count; i++) {
        Message* msg = &context->messages[i];
        cJSON* message = cJSON_CreateObject();
        
        switch (msg->role) {
            case ROLE_USER:
                cJSON_AddStringToObject(message, "role", "user");
                break;
            case ROLE_ASSISTANT:
                cJSON_AddStringToObject(message, "role", "assistant");
                break;
            case ROLE_SYSTEM:
                cJSON_AddStringToObject(message, "role", "system");
                break;
            case ROLE_TOOL:
                cJSON_AddStringToObject(message, "role", "tool");
                break;
        }
        
        cJSON_AddStringToObject(message, "content", msg->content);
        cJSON_AddItemToArray(messages, message);
    }
    cJSON_AddItemToObject(root, "messages", messages);
    
    cJSON_AddNumberToObject(root, "temperature", 0.7);
    cJSON_AddNumberToObject(root, "max_tokens", 1000);
    
    char* json_str = cJSON_Print(root);
    cJSON_Delete(root);
    return json_str;
}

// Parse model response
static ModelResponse* parse_response(const char* response) {
    ModelResponse* model_response = (ModelResponse*)malloc(sizeof(ModelResponse));
    if (!model_response) {
        return NULL;
    }
    
    cJSON* root = cJSON_Parse(response);
    if (!root) {
        model_response->content = NULL;
        model_response->error = strdup("Error parsing response");
        model_response->success = false;
        return model_response;
    }
    
    cJSON* error = cJSON_GetObjectItem(root, "error");
    if (error) {
        model_response->content = NULL;
        model_response->error = strdup(cJSON_GetStringValue(cJSON_GetObjectItem(error, "message")));
        model_response->success = false;
        cJSON_Delete(root);
        return model_response;
    }
    
    cJSON* choices = cJSON_GetObjectItem(root, "choices");
    if (choices && cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
        cJSON* choice = cJSON_GetArrayItem(choices, 0);
        cJSON* message = cJSON_GetObjectItem(choice, "message");
        cJSON* content = cJSON_GetObjectItem(message, "content");
        
        if (content) {
            model_response->content = strdup(cJSON_GetStringValue(content));
            model_response->error = NULL;
            model_response->success = true;
        } else {
            model_response->content = NULL;
            model_response->error = strdup("No content in response");
            model_response->success = false;
        }
    } else {
        model_response->content = NULL;
        model_response->error = strdup("Invalid response format");
        model_response->success = false;
    }
    
    cJSON_Delete(root);
    return model_response;
}

// Call model API
ModelResponse* model_call(const char* model, const char* api_key, const char* base_url, MessageList* context) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        ModelResponse* response = (ModelResponse*)malloc(sizeof(ModelResponse));
        response->content = NULL;
        response->error = strdup("Error initializing curl");
        response->success = false;
        return response;
    }
    
    char* api_url = (char*)malloc(strlen(base_url) + strlen("/chat/completions") + 1);
    sprintf(api_url, "%s/chat/completions", base_url);
    
    char* request_body = build_api_request(model, context);
    
    char* response_buffer = (char*)calloc(1, 1);
    
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    char* auth_header = (char*)malloc(strlen("Authorization: Bearer ") + strlen(api_key) + 1);
    sprintf(auth_header, "Authorization: Bearer %s", api_key);
    headers = curl_slist_append(headers, auth_header);
    
    curl_easy_setopt(curl, CURLOPT_URL, api_url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_buffer);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); // Disable SSL verification for testing
    
    CURLcode res = curl_easy_perform(curl);
    
    ModelResponse* model_response;
    if (res != CURLE_OK) {
        model_response = (ModelResponse*)malloc(sizeof(ModelResponse));
        model_response->content = NULL;
        model_response->error = strdup(curl_easy_strerror(res));
        model_response->success = false;
    } else {
        model_response = parse_response(response_buffer);
    }
    
    // Cleanup
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    free(api_url);
    free(request_body);
    free(response_buffer);
    free(auth_header);
    
    return model_response;
}

// Free model response
void model_response_free(ModelResponse* response) {
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