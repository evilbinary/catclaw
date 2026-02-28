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
typedef struct Channel {
    ChannelType type;
    char *name;
    bool enabled;
    bool connected;
    void *config;
    void (*connect)(struct Channel *channel);
    void (*disconnect)(struct Channel *channel);
    bool (*send_message)(struct Channel *channel, const char *message);
    bool (*receive_message)(struct Channel *channel, const char *message);
} Channel;

// Channel configuration structure
typedef struct {
    char *api_key;
    char *username;
    char *password;
    char *server;
    int port;
    bool enable_ssl;
} ChannelConfig;

// Message structure
typedef struct {
    ChannelType source;
    char *sender;
    char *content;
    char *timestamp;
} ChannelMessage;

// Functions
bool channels_init(void);
void channels_cleanup(void);
void channels_status(void);
bool channel_send_message(ChannelType type, const char *message);
bool channel_send_message_to_all(const char *message);
void channels_handle_websocket_message(const char *message);
bool channels_register_channel(ChannelType type, ChannelConfig *config);
bool channels_unregister_channel(ChannelType type);
void channels_process_message(ChannelMessage *message);
bool channel_enable(ChannelType type);
bool channel_disable(ChannelType type);
bool channel_connect(ChannelType type);
bool channel_disconnect(ChannelType type);
ChannelType channel_name_to_type(const char *name);

#endif // CHANNELS_H
