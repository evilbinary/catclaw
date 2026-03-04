#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>

// Configuration structure
typedef struct {
    char *workspace_path;      // 工作区路径
    char *model_provider;      // 模型提供商 (openai)
    char *model_name;          // 模型名称 (gpt-3.5-turbo)
    char *api_key;             // API 密钥
    char *api_base_url;        // API 基础 URL
    int max_context_tokens;    // 最大上下文 token 数
    int timeout_seconds;       // 超时时间（秒）
    int enable_compaction;     // 是否启用压缩
    int compaction_threshold;  // 压缩阈值（token 数）
    int gateway_port;          // 网关端口
    bool browser_enabled;      // 是否启用浏览器
} Config;

// Global config instance
extern Config g_config;

// Functions
bool config_load(void);
void config_cleanup(void);
void config_print(void);
bool config_set(const char *key, const char *value);
const char *config_get(const char *key);

#endif // CONFIG_H
