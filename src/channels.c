#include "channels.h"
#include "agent.h"
#include "telegram.h"
#include "discord.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Channel names
static const char *channel_names[] = {
    "WhatsApp",
    "Telegram",
    "Slack",
    "Discord",
    "Signal",
    "WebChat"
};

// Channels array
static Channel channels[CHANNEL_MAX];

// Default channel callback functions
static void default_connect(struct Channel *channel) {
    printf("Connecting to %s channel\n", channel->name);
    channel->connected = true;
}

static void default_disconnect(struct Channel *channel) {
    printf("Disconnecting from %s channel\n", channel->name);
    channel->connected = false;
}

static bool default_send_message(struct Channel *channel, const char *message) {
    printf("Sending message to %s: %s\n", channel->name, message);
    return true;
}

static bool default_receive_message(struct Channel *channel, const char *message) {
    printf("Receiving message from %s: %s\n", channel->name, message);
    return true;
}

bool channels_init(void) {
    // Initialize all channels
    for (int i = 0; i < CHANNEL_MAX; i++) {
        channels[i].type = i;
        channels[i].name = (char *)channel_names[i];
        channels[i].enabled = false;
        channels[i].connected = false;
        channels[i].config = NULL;
        channels[i].connect = default_connect;
        channels[i].disconnect = default_disconnect;
        channels[i].send_message = default_send_message;
        channels[i].receive_message = default_receive_message;
    }

    // Initialize Telegram channel
    telegram_channel_init(&channels[CHANNEL_TELEGRAM], NULL);
    
    // Initialize Discord channel
    discord_channel_init(&channels[CHANNEL_DISCORD], NULL);

    // Enable WebChat by default
    channels[CHANNEL_WEBCHAT].enabled = true;
    channels[CHANNEL_WEBCHAT].connected = true; // WebChat is always connected through WebSocket

    printf("Channels initialized\n");
    return true;
}

void channels_cleanup(void) {
    // Cleanup channels
    for (int i = 0; i < CHANNEL_MAX; i++) {
        if (channels[i].config) {
            free(channels[i].config);
            channels[i].config = NULL;
        }
    }
    printf("Channels cleaned up\n");
}

void channels_status(void) {
    printf("  Channels:\n");
    for (int i = 0; i < CHANNEL_MAX; i++) {
        printf("    %s: %s, %s\n", 
               channels[i].name,
               channels[i].enabled ? "enabled" : "disabled",
               channels[i].connected ? "connected" : "disconnected");
    }
}

bool channel_send_message(ChannelType type, const char *message) {
    if (type < 0 || type >= CHANNEL_MAX) {
        fprintf(stderr, "Invalid channel type\n");
        return false;
    }

    Channel *channel = &channels[type];
    if (!channel->enabled) {
        fprintf(stderr, "Channel %s is disabled\n", channel->name);
        return false;
    }

    if (!channel->connected) {
        fprintf(stderr, "Channel %s is not connected\n", channel->name);
        return false;
    }

    return channel->send_message(channel, message);
}

bool channel_send_message_to_all(const char *message) {
    bool success = true;
    for (int i = 0; i < CHANNEL_MAX; i++) {
        Channel *channel = &channels[i];
        if (channel->enabled && channel->connected) {
            if (!channel->send_message(channel, message)) {
                success = false;
            }
        }
    }
    return success;
}

// Function to handle WebSocket messages
void channels_handle_websocket_message(const char *message) {
    // Process WebSocket message
    printf("Received WebSocket message: %s\n", message);
    
    // Create channel message
    ChannelMessage msg;
    msg.source = CHANNEL_WEBCHAT;
    msg.sender = "webchat";
    msg.content = (char *)message;
    
    // Get current timestamp
    time_t now = time(NULL);
    char timestamp[20];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    msg.timestamp = timestamp;
    
    // Process the message
    channels_process_message(&msg);
}

bool channels_register_channel(ChannelType type, ChannelConfig *config) {
    if (type < 0 || type >= CHANNEL_MAX) {
        fprintf(stderr, "Invalid channel type\n");
        return false;
    }

    Channel *channel = &channels[type];
    
    // Allocate and copy config
    if (config) {
        ChannelConfig *new_config = (ChannelConfig *)malloc(sizeof(ChannelConfig));
        if (!new_config) {
            fprintf(stderr, "Memory allocation failed\n");
            return false;
        }
        *new_config = *config;
        channel->config = new_config;
    }
    
    channel->enabled = true;
    channel->connect(channel);
    
    printf("Channel %s registered\n", channel->name);
    return true;
}

bool channels_unregister_channel(ChannelType type) {
    if (type < 0 || type >= CHANNEL_MAX) {
        fprintf(stderr, "Invalid channel type\n");
        return false;
    }

    Channel *channel = &channels[type];
    
    if (channel->connected) {
        channel->disconnect(channel);
    }
    
    if (channel->config) {
        free(channel->config);
        channel->config = NULL;
    }
    
    channel->enabled = false;
    
    printf("Channel %s unregistered\n", channel->name);
    return true;
}

void channels_process_message(ChannelMessage *message) {
    // Process the message
    printf("Processing message from %s: %s\n", 
           channel_names[message->source], 
           message->content);
    
    // Forward message to agent
    agent_send_message(message->content);
    
    // TODO: Add message routing logic here
    // For example, forward to other channels if needed
}


