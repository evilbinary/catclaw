#include "telegram.h"
#include "common/http_client.h"
#include "common/cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Telegram 配置结构
typedef struct {
    char *bot_token;    // Bot Token
    char *chat_id;      // 目标聊天ID
} TelegramConfig;

// API 基础URL
#define TELEGRAM_API_BASE "https://api.telegram.org/bot"

// 清理 Telegram 配置
static void telegram_config_cleanup(TelegramConfig *config) {
    if (!config) return;
    free(config->bot_token);
    free(config->chat_id);
    free(config);
}

// 渠道清理函数
static void telegram_cleanup(ChannelInstance *channel) {
    if (channel->config) {
        telegram_config_cleanup((TelegramConfig *)channel->config);
        channel->config = NULL;
    }
}

// 连接 Telegram
static void telegram_connect(ChannelInstance *channel) {
    printf("[Telegram] Connecting channel '%s'\n", channel->name);
    
    TelegramConfig *config = (TelegramConfig *)channel->config;
    if (!config) {
        fprintf(stderr, "[Telegram] No config for channel '%s'\n", channel->name);
        return;
    }
    
    if (config->bot_token) {
        printf("[Telegram] Bot token configured\n");
        
        // 验证 bot token (获取 bot 信息)
        char url[256];
        snprintf(url, sizeof(url), "%s%s/getMe", TELEGRAM_API_BASE, config->bot_token);
        
        HttpResponse *resp = http_get(url);
        if (resp && resp->success) {
            printf("[Telegram] Bot token validated\n");
            channel->connected = true;
        } else {
            fprintf(stderr, "[Telegram] Failed to validate bot token\n");
        }
        
        if (resp) http_response_free(resp);
    } else {
        fprintf(stderr, "[Telegram] No bot token configured\n");
    }
}

// 断开连接
static void telegram_disconnect(ChannelInstance *channel) {
    printf("[Telegram] Disconnecting channel '%s'\n", channel->name);
    channel->connected = false;
}

// 发送消息
static bool telegram_send_message(ChannelInstance *channel, const char *message) {
    printf("[Telegram] Sending message via channel '%s'\n", channel->name);
    
    TelegramConfig *config = (TelegramConfig *)channel->config;
    if (!config || !config->bot_token) {
        fprintf(stderr, "[Telegram] No bot token configured\n");
        return false;
    }
    
    if (!config->chat_id) {
        fprintf(stderr, "[Telegram] No chat ID configured\n");
        return false;
    }
    
    // 构建请求URL
    char url[256];
    snprintf(url, sizeof(url), "%s%s/sendMessage", TELEGRAM_API_BASE, config->bot_token);
    
    // 构建请求体
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "chat_id", config->chat_id);
    cJSON_AddStringToObject(body, "text", message);
    
    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    
    // 发送请求
    HttpResponse *resp = http_post(url, body_str);
    free(body_str);
    
    if (!resp) return false;
    
    bool success = resp->success && resp->status_code == 200;
    http_response_free(resp);
    
    return success;
}

// 接收消息
static bool telegram_receive_message(ChannelInstance *channel, const char *message) {
    printf("[Telegram] Receiving message: %s\n", message);
    return true;
}

// 初始化 Telegram 渠道
void telegram_channel_init(ChannelInstance *channel, ChannelConfig *base_config) {
    // 创建 Telegram 特定配置
    TelegramConfig *config = (TelegramConfig *)calloc(1, sizeof(TelegramConfig));
    if (!config) {
        fprintf(stderr, "[Telegram] Memory allocation failed\n");
        return;
    }
    
    // 从基础配置复制值
    if (base_config) {
        if (base_config->bot_token) config->bot_token = strdup(base_config->bot_token);
        else if (base_config->api_key) config->bot_token = strdup(base_config->api_key);
        if (base_config->chat_id) config->chat_id = strdup(base_config->chat_id);
    }
    
    // 设置渠道属性
    channel->config = config;
    channel->connect = telegram_connect;
    channel->disconnect = telegram_disconnect;
    channel->send_message = telegram_send_message;
    channel->receive_message = telegram_receive_message;
    channel->cleanup = telegram_cleanup;
    
    printf("[Telegram] Channel '%s' initialized\n", channel->name);
}

// 设置 Telegram chat ID
void telegram_set_chat_id(ChannelInstance *channel, const char *chat_id) {
    TelegramConfig *config = (TelegramConfig *)channel->config;
    if (!config) return;
    
    free(config->chat_id);
    config->chat_id = chat_id ? strdup(chat_id) : NULL;
    
    printf("[Telegram] Chat ID set to: %s\n", chat_id);
}
