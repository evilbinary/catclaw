#ifndef TELEGRAM_H
#define TELEGRAM_H

#include "channels.h"

// 初始化 Telegram 渠道
void telegram_channel_init(ChannelInstance *channel, ChannelConfig *config);

// 设置 Telegram chat ID
void telegram_set_chat_id(ChannelInstance *channel, const char *chat_id);

#endif // TELEGRAM_H
