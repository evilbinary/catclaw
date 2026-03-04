#ifndef MODEL_H
#define MODEL_H

#include "message.h"

// Model response structure
typedef struct {
    char* content;
    char* error;
    bool success;
} ModelResponse;

// Functions
ModelResponse* model_call(const char* model, const char* api_key, const char* base_url, MessageList* context);
void model_response_free(ModelResponse* response);

#endif // MODEL_H