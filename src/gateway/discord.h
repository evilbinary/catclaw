#ifndef DISCORD_H
#define DISCORD_H

#include "channels.h"

// 初始化 Discord 渠道
void discord_channel_init(ChannelInstance *channel, ChannelConfig *config);

// 设置 Discord channel ID
void discord_set_channel_id(ChannelInstance *channel, const char *channel_id);

#endif // DISCORD_H
