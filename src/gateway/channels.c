#include "channels.h"
#include "agent/agent.h"
#include "common/config.h"
#include "telegram.h"
#include "discord.h"
#include "feishu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Channel type names
const char *channel_type_names[] = {
    "WhatsApp",
    "Telegram",
    "Slack",
    "Discord",
    "Signal",
    "WebChat",
    "Feishu"
};

// Global channel manager
static ChannelManager g_channel_manager = {NULL, 0};

// Default channel callback functions
static void default_connect(ChannelInstance *channel) {
    printf("[Channel] Connecting to %s (%s)\n", channel->name, channel_type_names[channel->type]);
    channel->connected = true;
}

static void default_disconnect(ChannelInstance *channel) {
    printf("[Channel] Disconnecting from %s (%s)\n", channel->name, channel_type_names[channel->type]);
    channel->connected = false;
}

static bool default_send_message(ChannelInstance *channel, const char *message) {
    printf("[Channel] Sending message to %s: %s\n", channel->name, message);
    return true;
}

static bool default_receive_message(ChannelInstance *channel, const char *message) {
    printf("[Channel] Receiving message from %s: %s\n", channel->name, message);
    return true;
}

static void default_cleanup(ChannelInstance *channel) {
    // Default cleanup does nothing
}

// Create a new channel instance
static ChannelInstance* channel_create(const char *id, ChannelType type, ChannelConfig *config) {
    ChannelInstance *channel = (ChannelInstance *)calloc(1, sizeof(ChannelInstance));
    if (!channel) {
        fprintf(stderr, "[Channel] Memory allocation failed\n");
        return NULL;
    }
    
    // Set basic properties
    channel->id = strdup(id);
    channel->name = config && config->name ? strdup(config->name) : strdup(channel_type_names[type]);
    channel->type = type;
    channel->enabled = true;
    channel->connected = false;
    channel->config = NULL;
    channel->user_data = NULL;
    channel->next = NULL;
    
    // Set default callbacks
    channel->connect = default_connect;
    channel->disconnect = default_disconnect;
    channel->send_message = default_send_message;
    channel->receive_message = default_receive_message;
    channel->cleanup = default_cleanup;
    
    // Initialize type-specific implementation
    switch (type) {
        case CHANNEL_TELEGRAM:
            telegram_channel_init(channel, config);
            break;
        case CHANNEL_DISCORD:
            discord_channel_init(channel, config);
            break;
        case CHANNEL_FEISHU:
            feishu_channel_init(channel, config);
            break;
        case CHANNEL_WEBCHAT:
            // WebChat uses default callbacks
            channel->connected = true;
            break;
        default:
            // Use default implementation
            if (config) {
                channel->config = malloc(sizeof(ChannelConfig));
                if (channel->config) {
                    memcpy(channel->config, config, sizeof(ChannelConfig));
                }
            }
            break;
    }
    
    return channel;
}

// Free a channel instance
static void channel_free(ChannelInstance *channel) {
    if (!channel) return;
    
    // Call type-specific cleanup
    if (channel->cleanup) {
        channel->cleanup(channel);
    }
    
    // Free basic properties
    free(channel->id);
    free(channel->name);
    
    // Free config (if not handled by type-specific cleanup)
    if (channel->config) {
        free(channel->config);
    }
    
    free(channel);
}

bool channels_init(void) {
    g_channel_manager.head = NULL;
    g_channel_manager.count = 0;
    
    // Load channels from configuration if available
    if (g_config.channels.count > 0) {
        channels_load_from_config();
    } else {
        // No channels configured, add default WebChat channel
        ChannelConfig webchat_config = {
            .id = "webchat-default",
            .name = "WebChat",
            .type = CHANNEL_WEBCHAT
        };
        ChannelInstance *webchat = channel_add("webchat-default", CHANNEL_WEBCHAT, &webchat_config);
        if (webchat) {
            webchat->enabled = true;
            webchat->connected = true;
        }
    }
    
    printf("[Channel] Manager initialized\n");
    return true;
}

void channels_cleanup(void) {
    ChannelInstance *current = g_channel_manager.head;
    while (current) {
        ChannelInstance *next = current->next;
        channel_free(current);
        current = next;
    }
    g_channel_manager.head = NULL;
    g_channel_manager.count = 0;
    printf("[Channel] Manager cleaned up\n");
}

void channels_status(void) {
    printf("Channels (%d):\n", g_channel_manager.count);
    ChannelInstance *current = g_channel_manager.head;
    while (current) {
        printf("  [%s] %s (%s): %s, %s\n",
               current->id,
               current->name,
               channel_type_names[current->type],
               current->enabled ? "enabled" : "disabled",
               current->connected ? "connected" : "disconnected");
        current = current->next;
    }
}

ChannelInstance* channel_add(const char *id, ChannelType type, ChannelConfig *config) {
    // Check if channel with same ID exists
    if (channel_find(id)) {
        fprintf(stderr, "[Channel] Channel with ID '%s' already exists\n", id);
        return NULL;
    }
    
    // Create new channel
    ChannelInstance *channel = channel_create(id, type, config);
    if (!channel) {
        return NULL;
    }
    
    // Add to list
    channel->next = g_channel_manager.head;
    g_channel_manager.head = channel;
    g_channel_manager.count++;
    
    printf("[Channel] Added channel '%s' (type: %s)\n", id, channel_type_names[type]);
    return channel;
}

bool channel_remove(const char *id) {
    ChannelInstance *prev = NULL;
    ChannelInstance *current = g_channel_manager.head;
    
    while (current) {
        if (strcmp(current->id, id) == 0) {
            if (prev) {
                prev->next = current->next;
            } else {
                g_channel_manager.head = current->next;
            }
            g_channel_manager.count--;
            
            // Disconnect before freeing
            if (current->connected && current->disconnect) {
                current->disconnect(current);
            }
            
            channel_free(current);
            printf("[Channel] Removed channel '%s'\n", id);
            return true;
        }
        prev = current;
        current = current->next;
    }
    
    fprintf(stderr, "[Channel] Channel '%s' not found\n", id);
    return false;
}

ChannelInstance* channel_find(const char *id) {
    ChannelInstance *current = g_channel_manager.head;
    while (current) {
        if (strcmp(current->id, id) == 0) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

ChannelInstance* channel_first_of_type(ChannelType type) {
    ChannelInstance *current = g_channel_manager.head;
    while (current) {
        if (current->type == type) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

bool channel_send_message(ChannelInstance *channel, const char *message) {
    if (!channel) {
        fprintf(stderr, "[Channel] Invalid channel\n");
        return false;
    }
    
    if (!channel->enabled) {
        fprintf(stderr, "[Channel] '%s' is disabled\n", channel->name);
        return false;
    }
    
    if (!channel->connected) {
        fprintf(stderr, "[Channel] '%s' is not connected\n", channel->name);
        return false;
    }
    
    // 如果渠道实现了流式发送回调，优先使用流式发送
    // 具体渠道（如飞书）会根据配置决定是否真正使用流式模式
    if (channel->stream_send) {
        return channel->stream_send(channel, message);
    }
    
    return channel->send_message(channel, message);
}

bool channel_send_message_by_id(const char *id, const char *message) {
    ChannelInstance *channel = channel_find(id);
    return channel_send_message(channel, message);
}

bool channel_send_message_to_all(const char *message) {
    bool success = true;
    ChannelInstance *current = g_channel_manager.head;
    while (current) {
        if (current->enabled && current->connected) {
            if (!channel_send_message(current, message)) {
                success = false;
            }
        }
        current = current->next;
    }
    return success;
}

bool channel_send_message_to_type(ChannelType type, const char *message) {
    bool success = true;
    ChannelInstance *current = g_channel_manager.head;
    while (current) {
        if (current->type == type && current->enabled && current->connected) {
            if (!channel_send_message(current, message)) {
                success = false;
            }
        }
        current = current->next;
    }
    return success;
}

// 流式发送消息 (打字机效果)
bool channel_stream_send(ChannelInstance *channel, const char *message) {
    if (!channel) {
        fprintf(stderr, "[Channel] Invalid channel\n");
        return false;
    }
    
    if (!channel->enabled) {
        fprintf(stderr, "[Channel] '%s' is disabled\n", channel->name);
        return false;
    }
    
    if (!channel->connected) {
        fprintf(stderr, "[Channel] '%s' is not connected\n", channel->name);
        return false;
    }
    
    // 如果渠道实现了流式发送回调，使用它
    if (channel->stream_send) {
        return channel->stream_send(channel, message);
    }
    
    // 否则回退到普通发送
    fprintf(stderr, "[Channel] '%s' does not support stream mode, falling back to normal send\n", channel->name);
    return channel->send_message(channel, message);
}

bool channel_stream_send_by_id(const char *id, const char *message) {
    ChannelInstance *channel = channel_find(id);
    return channel_stream_send(channel, message);
}

// 开始流式消息
bool channel_stream_start(ChannelInstance *channel, const char *initial_content) {
    if (!channel || !channel->enabled || !channel->connected) return false;
    if (channel->stream_start) {
        return channel->stream_start(channel, initial_content);
    }
    return false;
}

// 更新流式消息
bool channel_stream_update(ChannelInstance *channel, const char *content) {
    if (!channel || !channel->enabled || !channel->connected) return false;
    if (channel->stream_update) {
        return channel->stream_update(channel, content);
    }
    return false;
}

// 结束流式消息
bool channel_stream_end(ChannelInstance *channel) {
    if (!channel) return false;
    if (channel->stream_end) {
        return channel->stream_end(channel);
    }
    return false;
}

// 结束所有渠道的流式消息
void channel_stream_end_all(void) {
    ChannelInstance *current = g_channel_manager.head;
    while (current) {
        if (current->stream_end) {
            current->stream_end(current);
        }
        current = current->next;
    }
}

bool channel_enable(ChannelInstance *channel) {
    if (!channel) return false;
    channel->enabled = true;
    printf("[Channel] '%s' enabled\n", channel->name);
    return true;
}

bool channel_disable(ChannelInstance *channel) {
    if (!channel) return false;
    channel->enabled = false;
    if (channel->connected && channel->disconnect) {
        channel->disconnect(channel);
    }
    printf("[Channel] '%s' disabled\n", channel->name);
    return true;
}

bool channel_connect(ChannelInstance *channel) {
    if (!channel) return false;
    if (!channel->enabled) {
        fprintf(stderr, "[Channel] '%s' is disabled\n", channel->name);
        return false;
    }
    if (channel->connected) {
        fprintf(stderr, "[Channel] '%s' is already connected\n", channel->name);
        return false;
    }
    channel->connect(channel);
    return true;
}

bool channel_disconnect(ChannelInstance *channel) {
    if (!channel) return false;
    if (!channel->connected) {
        fprintf(stderr, "[Channel] '%s' is not connected\n", channel->name);
        return false;
    }
    channel->disconnect(channel);
    return true;
}

void channels_handle_websocket_message(const char *message) {
    printf("[Channel] Received WebSocket message: %s\n", message);
    
    // Find WebChat channel
    ChannelInstance *webchat = channel_first_of_type(CHANNEL_WEBCHAT);
    if (!webchat) return;
    
    // Create channel message
    ChannelMessage msg;
    msg.source = webchat;
    msg.source_type = CHANNEL_WEBCHAT;
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

void channels_process_message(ChannelMessage *message) {
    if (!message) return;
    
    printf("[Channel] Processing message from %s (%s): %s\n",
           message->source ? message->source->name : "unknown",
           channel_type_names[message->source_type],
           message->content);
    
    // Forward message to agent
    agent_send_message(message->content);
}

ChannelType channel_name_to_type(const char *name) {
    for (int i = 0; i < CHANNEL_MAX; i++) {
        if (strcasecmp(name, channel_type_names[i]) == 0) {
            return (ChannelType)i;
        }
    }
    // Also support alternate names
    if (strcasecmp(name, "lark") == 0) return CHANNEL_FEISHU;
    if (strcasecmp(name, "飞书") == 0) return CHANNEL_FEISHU;
    return CHANNEL_MAX;
}

const char* channel_type_to_name(ChannelType type) {
    if (type < 0 || type >= CHANNEL_MAX) return "Unknown";
    return channel_type_names[type];
}

void channels_foreach(ChannelIterator callback, void *user_data) {
    ChannelInstance *current = g_channel_manager.head;
    while (current) {
        callback(current, user_data);
        current = current->next;
    }
}

// Load channels from configuration
bool channels_load_from_config(void) {
    int loaded = 0;
    
    for (int i = 0; i < g_config.channels.count; i++) {
        ChannelConfigEntry *entry = &g_config.channels.channels[i];
        
        // Skip if not enabled
        if (!entry->enabled) {
            printf("[Channel] Skipping disabled channel '%s'\n", entry->id);
            continue;
        }
        
        // Determine channel type
        ChannelType type = channel_name_to_type(entry->type);
        if (type == CHANNEL_MAX) {
            fprintf(stderr, "[Channel] Unknown channel type: %s\n", entry->type);
            continue;
        }
        
        // Create channel config
        ChannelConfig config = {
            .id = entry->id,
            .name = entry->name,
            .type = type,
            .api_key = entry->api_key,
            .api_secret = entry->api_secret,
            .server = entry->server,
            .port = entry->port,
            .enable_ssl = entry->enable_ssl,
            .app_id = entry->app_id,
            .app_secret = entry->app_secret,
            .webhook_url = entry->webhook_url,
            .receive_id = entry->receive_id,
            .receive_id_type = entry->receive_id_type,
            .stream_mode = entry->stream_mode,
            .stream_speed = entry->stream_speed,
            .chat_id = entry->chat_id,
            .channel_id = entry->channel_id,
            .bot_token = entry->bot_token
        };
        
        // Add channel
        ChannelInstance *channel = channel_add(entry->id, type, &config);
        if (channel) {
            // Connect if enabled
            if (entry->enabled) {
                channel_connect(channel);
            }
            loaded++;
        }
    }
    
    printf("[Channel] Loaded %d channels from configuration\n", loaded);
    return loaded > 0 || g_config.channels.count == 0;
}
