#ifndef AI_MODEL_H
#define AI_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include "session/message.h"
#include "ai_provider.h"
#include "ai_provider_factory.h"

// 向后兼容的类型定义
typedef AIProviderType AIModelType;

#define AI_MODEL_OPENAI    AI_PROVIDER_OPENAI
#define AI_MODEL_ANTHROPIC AI_PROVIDER_ANTHROPIC
#define AI_MODEL_GEMINI    AI_PROVIDER_GEMINI
#define AI_MODEL_LLAMA     AI_PROVIDER_OLLAMA

// 配置（复用 Provider 配置）
typedef AIProviderConfig AIModelConfig;

// 响应（复用 Provider 响应）
typedef AIProviderResponse AIModelResponse;

// ==================== 流式回调 ====================

// 流式输出回调函数类型 (chunk: 当前片段, accumulated: 累积内容)
typedef void (*AIStreamCallback)(const char* chunk, const char* accumulated, void* user_data);

// 设置当前 provider 的流式回调
void ai_model_set_stream_callback(AIStreamCallback callback, void* user_data);

// ==================== 统一入口 API ====================

// 初始化/清理
bool ai_model_init(const AIModelConfig* config);
void ai_model_cleanup(void);

// 配置
bool ai_model_set_config(const AIModelConfig* config);

// 发送消息
AIModelResponse* ai_model_send_message(const char* message);
AIModelResponse* ai_model_send_messages(MessageList* messages, const char* system_prompt);

// 释放响应
void ai_model_free_response(AIModelResponse* response);

// ==================== Provider 管理 ====================

// 获取当前 Provider
AIProvider* ai_model_get_provider(void);

// 切换 Provider
bool ai_model_switch_provider(AIProviderType type, const AIModelConfig* config);

#endif // AI_MODEL_H
