#ifndef AI_PROVIDER_FACTORY_H
#define AI_PROVIDER_FACTORY_H

#include "ai_provider.h"

// 已注册的提供者类型
typedef enum {
    AI_PROVIDER_OPENAI,       // OpenAI 及兼容 API (doubao, deepseek 等)
    AI_PROVIDER_ANTHROPIC,    // Anthropic Claude
    AI_PROVIDER_GEMINI,       // Google Gemini
    AI_PROVIDER_OLLAMA,       // Ollama 本地模型
    AI_PROVIDER_COUNT
} AIProviderType;

// 工厂函数
AIProvider* ai_provider_create(AIProviderType type, const AIProviderConfig* config);
void ai_provider_destroy(AIProvider* provider);

// 注册自定义提供者
typedef AIProvider* (*ProviderCreateFunc)(const AIProviderConfig* config);
bool ai_provider_register(AIProviderType type, ProviderCreateFunc create_func);

// 工具函数：根据字符串获取类型
AIProviderType ai_provider_type_from_string(const char* name);
const char* ai_provider_type_to_string(AIProviderType type);

#endif // AI_PROVIDER_FACTORY_H
