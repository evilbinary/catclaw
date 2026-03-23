#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>

// Model configuration
typedef struct {
    char *name;               // 模型配置名称（用于切换）
    char *provider;           // 模型提供商 (llama/openai/anthropic/gemini)
    char *model_name;         // 模型名称
    char *base_url;           // API 基础 URL
    char *api_key;            // API 密钥
    int max_context_tokens;   // 最大上下文 token 数
    int timeout_seconds;      // 超时时间（秒）
    float temperature;        // 温度参数
    int max_tokens;           // 最大生成 token 数
    bool stream;              // 是否启用流式响应（打字机效果）
} ModelConfig;

// Models array configuration
typedef struct {
    ModelConfig *models;      // 模型配置数组
    int count;                // 模型数量
    int capacity;             // 数组容量
    int current_index;        // 当前使用的模型索引
    int default_index;        // 默认模型索引
    char *default_model;      // 默认模型名称（优先使用）
} ModelsConfig;

// Gateway configuration
typedef struct {
    int port;                 // 网关端口
    bool browser_enabled;     // 是否启用浏览器
    char *http_api_key;       // HTTP API 授权密钥 (可选)
    bool http_auth_enabled;   // 是否启用 HTTP 授权
    bool http_server_enabled; // 是否启用 HTTP API 服务器
    bool websocket_enabled;   // 是否启用 WebSocket 服务器
} GatewayConfig;

// Workspace configuration
typedef struct {
    char *path;               // 工作区路径
} WorkspaceConfig;

// Session configuration
typedef struct {
    int max_sessions;         // 最大会话数量
    bool auto_save;           // 是否自动保存会话
    char *default_session_key; // 默认会话密钥
    int max_history_per_session; // 每个会话的最大历史消息数
    int context_history_limit; // 构建上下文时加载的历史消息数量限制（默认5条）
} SessionConfig;

// Logging configuration
typedef struct {
    char *level;              // 日志级别 (debug/info/warn/error/fatal)
    char *file;               // 日志文件路径
    bool console_output;      // 是否输出到控制台
} LoggingConfig;

// Compaction configuration
typedef struct {
    bool enabled;             // 是否启用压缩
    int threshold;            // 压缩阈值（token 数）
} CompactionConfig;

// Agent configuration
typedef struct {
    char *system_prompt;      // 系统提示词
} AgentConfig;

// Channel configuration for config file
typedef struct {
    char *id;                    // 唯一标识符
    char *name;                  // 显示名称
    char *type;                  // 渠道类型: telegram, discord, feishu, webchat, etc.
    
    // 通用配置
    char *api_key;
    char *api_secret;
    char *server;
    int port;
    bool enable_ssl;
    bool enabled;                // 是否启用
    
    // 飞书专用
    char *app_id;
    char *app_secret;
    char *webhook_url;
    char *receive_id;            // 接收消息的用户/群组ID
    char *receive_id_type;       // 接收ID类型: open_id, user_id, union_id, chat_id
    bool stream_mode;            // 是否启用流式输出（打字机效果）
    int stream_speed;            // 流式输出速度（字符/秒），默认 20
    bool ws_enabled;             // 是否启用 WebSocket 事件订阅
    char *ws_domain;             // WebSocket 连接域名 (可选)
    int ws_ping_interval;        // WebSocket 心跳间隔(秒)，默认 120
    int ws_reconnect_interval;   // WebSocket 重连间隔(秒)，默认 120
    int ws_max_reconnect;        // WebSocket 最大重连次数，-1 表示无限
    
    // Telegram 专用
    char *chat_id;
    
    // Discord 专用
    char *channel_id;
    char *bot_token;
} ChannelConfigEntry;

// Channels configuration
typedef struct {
    ChannelConfigEntry *channels;  // 渠道配置数组
    int count;                      // 渠道数量
    int capacity;                   // 数组容量
    int current_index;              // 当前使用的渠道索引
    int default_index;              // 默认渠道索引
    char *default_channel;          // 默认渠道名称（优先使用）
} ChannelsConfig;

// Main configuration structure
typedef struct {
    ModelsConfig models;      // 多模型配置
    ModelConfig model;        // 当前选中的模型（兼容旧代码）
    GatewayConfig gateway;
    WorkspaceConfig workspace;
    SessionConfig session;
    LoggingConfig logging;
    CompactionConfig compaction;
    AgentConfig agent;
    ChannelsConfig channels;  // 多渠道配置
    
    // Legacy fields for backward compatibility
    char *workspace_path;
    char *model_provider;
    char *model_name;
    char *api_key;
    char *api_base_url;
    int max_context_tokens;
    int timeout_seconds;
    int enable_compaction;
    int compaction_threshold;
    int gateway_port;
    int http_port;            // HTTP API 端口
    bool browser_enabled;
    bool debug;
} Config;

// Global config instance
extern Config g_config;

// Functions
bool config_load(void);
void config_cleanup(void);
void config_print(void);
bool config_set(const char *key, const char *value);
const char *config_get(const char *key);
char *get_home_dir(void);

// Model management functions
bool config_switch_model(const char *model_name);
bool config_switch_model_by_index(int index);
void config_list_models(void);
const char* config_get_current_model_name(void);

// Channel management functions
void config_list_channels(void);
int config_get_channel_count(void);
ChannelConfigEntry* config_get_channel(int index);
ChannelConfigEntry* config_get_default_channel(void);
ChannelConfigEntry* config_get_current_channel(void);
bool config_switch_channel(const char *channel_id_or_name);

#endif // CONFIG_H
