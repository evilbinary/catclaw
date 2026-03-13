#ifndef TELEGRAM_H
#define TELEGRAM_H

#include "channels.h"

// Initialize Telegram channel
void telegram_channel_init(Channel *channel, ChannelConfig *base_config);

// Set Telegram chat ID
void telegram_set_chat_id(Channel *channel, const char *chat_id);

#endif // TELEGRAM_H
