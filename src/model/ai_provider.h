#ifndef AI_PROVIDER_H
#define AI_PROVIDER_H

#include <stdbool.h>
#include <stddef.h>
#include "session/message.h"

// 流式回调函数类型 (chunk: 当前片段, accumulated: 累积内容)
typedef void (*AIStreamCallback)(const char* chunk, const char* accumulated, void* user_data);

// Provider 配置
typedef struct {
    char* api_key;
    char* model_name;
    char* base_url;
    char* provider;  // 提供商类型 (openai/ollama/anthropic/gemini)
    float temperature;
    int max_tokens;
    bool stream;
    AIStreamCallback stream_callback;  // 流式回调
    void* stream_user_data;            // 流式回调用户数据
} AIProviderConfig;

// 响应结构
typedef struct {
    char* content;
    bool success;
    char* error;
    char* tool_calls;  // JSON string
} AIProviderResponse;

// Provider 接口
typedef struct AIProvider AIProvider;

struct AIProvider {
    const char* name;           // 提供者名称
    AIProviderConfig config;    // 配置
    void* impl;                 // 私有实现数据
    
    // 接口方法
    bool (*init)(AIProvider* self);
    void (*destroy)(AIProvider* self);
    AIProviderResponse* (*send_messages)(AIProvider* self,
                                          MessageList* messages,
                                          const char* system_prompt);
    void (*free_response)(AIProviderResponse* response);
};

// 创建/销毁配置
AIProviderConfig* ai_provider_config_create(void);
void ai_provider_config_destroy(AIProviderConfig* config);
void ai_provider_config_set(AIProviderConfig* config,
                            const char* api_key,
                            const char* model_name,
                            const char* base_url,
                            float temperature,
                            int max_tokens,
                            bool stream);

// 创建/销毁响应
AIProviderResponse* ai_provider_response_create(const char* content, bool success, const char* error);
void ai_provider_response_destroy(AIProviderResponse* response);

#endif // AI_PROVIDER_H
