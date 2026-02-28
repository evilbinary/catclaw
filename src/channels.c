#include "channels.h"
#include "agent.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

bool channels_init(void) {
    // Initialize all channels
    for (int i = 0; i < CHANNEL_MAX; i++) {
        channels[i].type = i;
        channels[i].name = (char *)channel_names[i];
        channels[i].enabled = false;
        channels[i].connected = false;
    }

    // Enable WebChat by default
    channels[CHANNEL_WEBCHAT].enabled = true;
    channels[CHANNEL_WEBCHAT].connected = true; // WebChat is always connected through WebSocket

    printf("Channels initialized\n");
    return true;
}

void channels_cleanup(void) {
    // Cleanup channels
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

    switch (type) {
        case CHANNEL_WEBCHAT:
            // Send message through WebSocket
            printf("Sending message to WebChat: %s\n", message);
            // TODO: Implement actual WebSocket message sending
            // For now, just print the message
            return true;

        default:
            // TODO: Implement other channel message sending
            printf("Sending message to %s: %s\n", channel->name, message);
            return true;
    }
}

// Function to handle WebSocket messages
void channels_handle_websocket_message(const char *message) {
    // Process WebSocket message
    printf("Received WebSocket message: %s\n", message);
    
    // Forward message to agent
    agent_send_message(message);
}

