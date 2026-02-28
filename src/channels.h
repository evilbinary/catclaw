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
    CHANNEL_MAX
} ChannelType;

// Channel structure
typedef struct {
    ChannelType type;
    char *name;
    bool enabled;
    bool connected;
} Channel;

// Functions
bool channels_init(void);
void channels_cleanup(void);
void channels_status(void);
bool channel_send_message(ChannelType type, const char *message);
void channels_handle_websocket_message(const char *message);

#endif // CHANNELS_H
