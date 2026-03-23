#ifndef CHANNELS_H
#define CHANNELS_H

#include <stdbool.h>

// Channel types
typedef enum {
    CHANNEL_WHATSAPP,
    CHANNEL_TELEGRAM,
    CHANNEL_SLACK,
    CHANNEL_DISCORD,
    CHANNEL_SIGNAL,
    CHANNEL_WEBCHAT,
    CHANNEL_FEISHU,      // 飞书
    CHANNEL_MAX
} ChannelType;

// Channel type names
extern const char *channel_type_names[];

// Forward declaration
typedef struct ChannelInstance ChannelInstance;

// Channel structure (single instance)
struct ChannelInstance {
    char *id;                    // 唯一标识符
    char *name;                  // 显示名称
    ChannelType type;            // 渠道类型
    bool enabled;                // 是否启用
    bool connected;              // 是否已连接
    void *config;                // 渠道特定配置
    void *user_data;             // 用户数据
    void *stream_ctx;            // 流式消息上下文
    
    // Callback functions
    void (*connect)(ChannelInstance *channel);
    void (*disconnect)(ChannelInstance *channel);
    bool (*send_message)(ChannelInstance *channel, const char *message);
    bool (*receive_message)(ChannelInstance *channel, const char *message);
    void (*cleanup)(ChannelInstance *channel);
    
    // 流式发送回调 (可选，用于打字机效果)
    bool (*stream_send)(ChannelInstance *channel, const char *message);
    bool (*stream_start)(ChannelInstance *channel, const char *initial_content);
    bool (*stream_update)(ChannelInstance *channel, const char *content);
    bool (*stream_end)(ChannelInstance *channel);
    
    ChannelInstance *next;       // 链表下一个节点
};

// Channel manager
typedef struct {
    ChannelInstance *head;       // 渠道链表头
    int count;                   // 渠道数量
} ChannelManager;

// Channel configuration structure (for loading from config)
typedef struct {
    char *id;                    // 唯一标识符
    char *name;                  // 显示名称
    ChannelType type;            // 渠道类型
    
    // 通用配置
    char *api_key;
    char *api_secret;
    char *server;
    int port;
    bool enable_ssl;
    
    // 飞书专用
    char *app_id;
    char *app_secret;
    char *webhook_url;
    char *receive_id;            // 接收消息的用户/群组ID
    char *receive_id_type;       // 接收ID类型: open_id, user_id, union_id, chat_id
    bool stream_mode;            // 是否启用流式输出（打字机效果）
    int stream_speed;            // 流式输出速度（字符/秒）
    
    // Telegram 专用
    char *chat_id;
    
    // Discord 专用
    char *channel_id;
    char *bot_token;
} ChannelConfig;

// Message structure
typedef struct {
    ChannelInstance *source;     // 来源渠道实例
    ChannelType source_type;     // 来源渠道类型
    char *sender;                // 发送者
    char *content;               // 消息内容
    char *timestamp;             // 时间戳
} ChannelMessage;

// Functions - Channel Manager
bool channels_init(void);
void channels_cleanup(void);
void channels_status(void);
bool channels_load_from_config(void);

// Functions - Channel Instance Management
ChannelInstance* channel_add(const char *id, ChannelType type, ChannelConfig *config);
bool channel_remove(const char *id);
ChannelInstance* channel_find(const char *id);
ChannelInstance* channel_find_by_type(ChannelType type);
ChannelInstance* channel_first_of_type(ChannelType type);

// Functions - Channel Operations
bool channel_send_message(ChannelInstance *channel, const char *message);
bool channel_send_message_by_id(const char *id, const char *message);
bool channel_send_message_to_all(const char *message);
bool channel_send_message_to_type(ChannelType type, const char *message);
bool channel_stream_send(ChannelInstance *channel, const char *message);
bool channel_stream_send_by_id(const char *id, const char *message);

// 流式消息操作 (用于实时更新消息内容)
bool channel_stream_start(ChannelInstance *channel, const char *initial_content);
bool channel_stream_update(ChannelInstance *channel, const char *content);
bool channel_stream_end(ChannelInstance *channel);
void channel_stream_end_all(void);  // 结束所有渠道的流式消息

bool channel_enable(ChannelInstance *channel);
bool channel_disable(ChannelInstance *channel);
bool channel_connect(ChannelInstance *channel);
bool channel_disconnect(ChannelInstance *channel);

// Functions - Message Processing
void channels_handle_websocket_message(const char *message);
void channels_process_message(ChannelMessage *message);

// Functions - Utilities
ChannelType channel_name_to_type(const char *name);
const char* channel_type_to_name(ChannelType type);

// Functions - Iteration
typedef void (*ChannelIterator)(ChannelInstance *channel, void *user_data);
void channels_foreach(ChannelIterator callback, void *user_data);

#endif // CHANNELS_H
