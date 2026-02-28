#ifndef AI_MODEL_H
#define AI_MODEL_H

#include <stdbool.h>
#include <stddef.h>

// AI model types
typedef enum {
    AI_MODEL_OPENAI,
    AI_MODEL_ANTHROPIC,
    AI_MODEL_GEMINI,
    AI_MODEL_LLAMA
} AIModelType;

// AI model configuration
typedef struct {
    AIModelType type;
    char *api_key;
    char *model_name;
    char *base_url;
} AIModelConfig;

// AI model response
typedef struct {
    char *content;
    bool success;
    char *error;
} AIModelResponse;

// Functions
bool ai_model_init(const AIModelConfig *config);
void ai_model_cleanup(void);
bool ai_model_set_config(const AIModelConfig *config);
AIModelResponse *ai_model_send_message(const char *message);
void ai_model_free_response(AIModelResponse *response);

#endif // AI_MODEL_H
