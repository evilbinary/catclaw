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

#endif // FEISHU_H
