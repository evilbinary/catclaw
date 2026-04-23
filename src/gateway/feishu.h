#ifndef FEISHU_H
#define FEISHU_H

#include "channels.h"

// 飞书渠道初始化
void feishu_channel_init(ChannelInstance *channel, ChannelConfig *config);

// 设置飞书接收ID
void feishu_set_receive_id(ChannelInstance *channel, const char *receive_id, const char *receive_id_type);

// 获取飞书 tenant_access_token
char* feishu_get_tenant_access_token(const char *app_id, const char *app_secret);

// 处理飞书事件回调 (HTTP请求体，返回响应JSON，需调用者释放)
char* feishu_handle_event(const char *body);

// 飞书消息结构
typedef struct {
    char *sender_id;       // 发送者ID
    char *sender_type;     // 发送者类型 (open_id, user_id, union_id)
    char *message_id;      // 消息ID
    char *message_type;    // 消息类型 (text, post, image等)
    char *content;         // 消息内容
    char *chat_id;         // 群聊ID (如果是群消息)
    char *channel_id;      // 飞书渠道实例ID (用于回复)
} FeishuMessage;

// 解析飞书消息事件
FeishuMessage* feishu_parse_message(const char *event_json);
void feishu_message_free(FeishuMessage *msg);

// 回复飞书消息
bool feishu_reply_message(const char *channel_id, const char *message_id, const char *content);

// 回复飞书消息（通过 chat_id/open_id）
bool feishu_reply_to_chat(const char *chat_id, const char *content);

// 发送图片消息
bool feishu_send_image(const char *chat_id, const char *image_path);
bool feishu_send_image_url(const char *chat_id, const char *image_url);

// 上传图片到飞书 (返回 image_key，需调用者释放)
char* feishu_upload_image(const char *file_path);

// 下载飞书图片 (返回 base64 编码数据，需调用者释放)
char* feishu_download_image(const char *image_key);

// ==================== 流式消息 API (打字机效果) ====================

// 流式消息上下文
typedef struct {
    char *message_id;      // 流式消息ID
    char *channel_id;      // 渠道ID
    char *access_token;    // 访问令牌
    char *current_content; // 当前内容（用于结束流式时发送）
    bool active;           // 是否激活
} FeishuStreamContext;

// 创建流式消息 (返回 message_id，需调用者释放)
char* feishu_stream_create(const char *channel_id, const char *receive_id, const char *receive_id_type);

// 更新流式消息内容
bool feishu_stream_update(const char *channel_id, const char *message_id, const char *content);

// 结束流式消息
bool feishu_stream_finish(const char *channel_id, const char *message_id);

// 流式发送完整消息 (模拟打字机效果，按 stream_speed 更新内容)
bool feishu_stream_send(const char *channel_id, const char *message, int speed_chars_per_sec);

#endif // FEISHU_H
