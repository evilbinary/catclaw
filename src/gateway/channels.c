#include "channels.h"
#include "agent/agent.h"
#include "agent/command.h"
#include "common/config.h"
#include "common/log.h"
#include "telegram.h"
#include "discord.h"
#include "feishu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

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
static ChannelManager g_channel_manager = {NULL, 0, NULL};

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

static void default_cleanup(ChannelInstance *channel) {
    // Default cleanup does nothing
    (void)channel;  // suppress unused parameter warning
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
    channel->stream_ctx = NULL;
    
    // 初始化流式队列
    channel->stream_queue_head = NULL;
    channel->stream_queue_tail = NULL;
    channel->stream_processing = false;
    pthread_mutex_init(&channel->stream_mutex, NULL);
    
    // Set default callbacks
    channel->connect = default_connect;
    channel->disconnect = default_disconnect;
    channel->send_message = default_send_message;
    channel->cleanup = default_cleanup;
    channel->receive_message = NULL;  // 默认无自定义处理
    
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
    
    // 清理流式队列
    pthread_mutex_destroy(&channel->stream_mutex);
    StreamTaskNode* curr = channel->stream_queue_head;
    while (curr) {
        StreamTaskNode* next = curr->next;
        free(curr->content);
        free(curr);
        curr = next;
    }
    
    free(channel);
}

bool channels_init(void) {
    g_channel_manager.head = NULL;
    g_channel_manager.count = 0;
    
    // 创建线程池（4个工作线程，128个任务队列）
    g_channel_manager.pool = thread_pool_create(4, 128);
    if (!g_channel_manager.pool) {
        fprintf(stderr, "[Channel] Failed to create thread pool\n");
        return false;
    }
    
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
    
    printf("[Channel] Manager initialized with thread pool\n");
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
    
    // 销毁线程池
    if (g_channel_manager.pool) {
        thread_pool_destroy(g_channel_manager.pool);
        g_channel_manager.pool = NULL;
    }
    
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

// 构建带上下文的消息
char* channel_build_context_message(ChannelInstance *channel, ChannelIncomingMessage *msg) {
    if (!channel || !msg || !msg->content) return NULL;

    size_t size = 256 + strlen(msg->content);
    char* buf = (char*)malloc(size);
    if (!buf) return NULL;

    snprintf(buf, size, "[%s:%s:%s] %s",
             channel_type_names[channel->type],
             msg->sender_id ? msg->sender_id : "unknown",
             msg->chat_id ? msg->chat_id : (msg->message_id ? msg->message_id : "unknown"),
             msg->content);

    return buf;
}

// 统一消息处理入口
bool channel_handle_incoming_message(ChannelInstance *channel, ChannelIncomingMessage *msg, char **out_response) {
    if (!channel || !msg || !msg->content) {
        return false;
    }

    // 1. 首先检查是否是命令
    if (msg->content[0] == '/') {
        char* cmd_response = command_handle(msg->content);
        if (cmd_response) {
            log_info("[Channel] Command received on %s: %s", channel->name, msg->content);
            *out_response = cmd_response;
            return true;  // 命令已处理
        }
    }

    // 2. 调用渠道特定的消息接收回调
    if (channel->receive_message) {
        char* custom_response = NULL;
        if (channel->receive_message(channel, msg, &custom_response)) {
            if (custom_response) {
                *out_response = custom_response;
            }
            return true;  // 渠道已处理
        }
        // 渠道未处理，继续默认流程
        if (custom_response) {
            free(custom_response);
        }
    }

    // 3. 发送到 agent 处理
    char* context_msg = channel_build_context_message(channel, msg);
    if (context_msg) {
        if (agent_send_message(context_msg)) {
            log_info("[Channel] Message sent to agent from %s", channel->name);
        } else {
            log_error("[Channel] Failed to send message to agent from %s", channel->name);
        }
        free(context_msg);
    }

    return false;  // 消息已发送到 agent，没有即时响应
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

// ==================== 流式消息任务实现 ====================

#define STREAM_MIN_INTERVAL_MS 50  // 流式消息最小间隔（毫秒）

// 处理单个流式任务
static void process_stream_task(ChannelInstance *channel, StreamTaskType type, const char *content) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    if (type == STREAM_TASK_START) {
        // 开始流式消息
        if (channel->stream_start) {
            channel->stream_start(channel, content);
        }
    } else if (type == STREAM_TASK_END) {
        // 结束流式消息
        if (channel->stream_end) {
            channel->stream_end(channel);
        }
    } else if (type == STREAM_TASK_UPDATE && content) {
        // 更新消息
        if (channel->stream_update) {
            channel->stream_update(channel, content);
        }
        // 飞书 API 已经很慢(300-800ms)，不需要额外延迟
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    long cost_ms = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000;
    if (type == STREAM_TASK_UPDATE) {
        log_debug("[TIMING] process_task UPDATE: cost=%ldms, len=%zu", cost_ms, content ? strlen(content) : 0);
    }
}

// 处理 channel 串行队列的任务（在线程池中执行）
static void stream_process_queue(void *arg) {
    ChannelInstance *channel = (ChannelInstance *)arg;
    if (!channel) return;
    
    while (1) {
        // 在 mutex 内取出任务
        pthread_mutex_lock(&channel->stream_mutex);
        StreamTaskNode *node = channel->stream_queue_head;
        if (node) {
            channel->stream_queue_head = node->next;
            if (!channel->stream_queue_head) {
                channel->stream_queue_tail = NULL;
            }
        }
        
        // 如果队列为空，标记处理完成并退出
        if (!node) {
            channel->stream_processing = false;
            pthread_mutex_unlock(&channel->stream_mutex);
            break;
        }
        pthread_mutex_unlock(&channel->stream_mutex);
        
        // 在 mutex 外处理任务
        process_stream_task(channel, node->type, node->content);
        
        // END 任务后退出
        if (node->type == STREAM_TASK_END) {
            free(node->content);
            free(node);
            // 重新标记处理完成
            pthread_mutex_lock(&channel->stream_mutex);
            channel->stream_processing = false;
            pthread_mutex_unlock(&channel->stream_mutex);
            break;
        }
        
        free(node->content);
        free(node);
    }
}

// 提交流式任务到串行队列
void channel_stream_submit_task(ChannelInstance *channel, StreamTaskType type, const char *content) {
    if (!channel || !g_channel_manager.pool) return;
    
    // 创建任务节点
    StreamTaskNode *node = (StreamTaskNode *)calloc(1, sizeof(StreamTaskNode));
    if (!node) return;
    node->type = type;
    node->content = content ? strdup(content) : NULL;
    node->next = NULL;
    
    // 在 mutex 内加入队列并检查处理状态
    pthread_mutex_lock(&channel->stream_mutex);
    
    // 加入队列
    if (channel->stream_queue_tail) {
        channel->stream_queue_tail->next = node;
    } else {
        channel->stream_queue_head = node;
    }
    channel->stream_queue_tail = node;
    
    // 检查是否需要提交处理任务
    bool should_submit = !channel->stream_processing;
    if (should_submit) {
        channel->stream_processing = true;
    }
    
    pthread_mutex_unlock(&channel->stream_mutex);
    
    if (should_submit) {
        thread_pool_add_task(g_channel_manager.pool, stream_process_queue, channel);
    }
}
