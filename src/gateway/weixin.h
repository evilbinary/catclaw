#ifndef WEIXIN_H
#define WEIXIN_H

#include "channels.h"
#include <stdbool.h>

// iLink API 基础URL
#define WEIXIN_ILINK_BASE_URL "https://ilinkai.weixin.qq.com"

// 微信消息结构
typedef struct {
    char *from_user_id;      // 发送者ID (xxx@im.wechat)
    char *to_user_id;        // 接收者ID (xxx@im.bot)
    int message_type;        // 1=用户消息, 2=Bot消息
    int message_state;       // 2=FINISH
    char *context_token;     // 对话关联token（回复时必须带上）
    char *text;              // 文本内容
    // 媒体文件相关
    int item_type;           // 1=文本, 2=图片, 3=语音, 4=文件, 5=视频
} WeixinMessage;

// 微信流式消息上下文
typedef struct {
    char *from_user_id;      // 当前回复目标
    char *context_token;     // 对话关联token
    char *accumulated;       // 累积的消息内容
    bool active;             // 是否活跃
} WeixinStreamContext;

// 微信配置结构
typedef struct {
    char *bot_token;         // iLink bot token（登录后获取）
    char *base_url;          // API基础URL
    char *get_updates_buf;   // 长轮询游标
    bool is_logged_in;       // 是否已登录
    bool stream_mode;        // 是否启用流式输出
} WeixinConfig;

// 初始化微信channel
bool weixin_channel_init(ChannelInstance *channel, ChannelConfig *config);

// 登录相关
bool weixin_get_qrcode(char **qrcode, char **qrcode_img);
bool weixin_check_qrcode_status(const char *qrcode, char **bot_token, char **base_url);

// 消息收发
bool weixin_get_updates(const char *bot_token, const char *cursor, 
                        WeixinMessage **messages, int *msg_count, char **new_cursor);
bool weixin_send_message(const char *bot_token, const char *to_user_id, 
                         const char *text, const char *context_token);

// 清理消息数组
void weixin_free_messages(WeixinMessage *messages, int count);

#endif // WEIXIN_H
