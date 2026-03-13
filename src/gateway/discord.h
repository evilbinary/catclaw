#ifndef DISCORD_H
#define DISCORD_H

#include "channels.h"

// Initialize Discord channel
void discord_channel_init(Channel *channel, ChannelConfig *base_config);

// Set Discord channel ID
void discord_set_channel_id(Channel *channel, const char *channel_id);

#endif // DISCORD_H
