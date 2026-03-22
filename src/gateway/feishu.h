#ifndef FEISHU_H
#define FEISHU_H

#include "channels.h"

// 飞书渠道初始化
void feishu_channel_init(ChannelInstance *channel, ChannelConfig *config);

// 设置飞书接收ID
void feishu_set_receive_id(ChannelInstance *channel, const char *receive_id, const char *receive_id_type);

// 获取飞书 tenant_access_token
char* feishu_get_tenant_access_token(const char *app_id, const char *app_secret);

#endif // FEISHU_H
