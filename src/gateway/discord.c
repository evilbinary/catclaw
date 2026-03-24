#include "discord.h"
#include "common/http_client.h"
#include "common/cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Discord 配置结构
typedef struct {
    char *bot_token;    // Bot Token
    char *channel_id;   // 目标频道ID
    char *webhook_url;  // Webhook URL (备选方案)
} DiscordConfig;

// API 基础URL
#define DISCORD_API_BASE "https://discord.com/api/v10"

// 清理 Discord 配置
static void discord_config_cleanup(DiscordConfig *config) {
    if (!config) return;
    free(config->bot_token);
    free(config->channel_id);
    free(config->webhook_url);
    free(config);
}

// 渠道清理函数
static void discord_cleanup(ChannelInstance *channel) {
    if (channel->config) {
        discord_config_cleanup((DiscordConfig *)channel->config);
        channel->config = NULL;
    }
}

// 连接 Discord
static void discord_connect(ChannelInstance *channel) {
    printf("[Discord] Connecting channel '%s'\n", channel->name);
    
    DiscordConfig *config = (DiscordConfig *)channel->config;
    if (!config) {
        fprintf(stderr, "[Discord] No config for channel '%s'\n", channel->name);
        return;
    }
    
    if (config->webhook_url) {
        printf("[Discord] Using webhook mode\n");
        channel->connected = true;
    } else if (config->bot_token) {
        printf("[Discord] Bot token configured\n");
        
        // 验证 bot token (获取当前用户信息)
        char url[256];
        snprintf(url, sizeof(url), "%s/users/@me", DISCORD_API_BASE);
        
        char auth_header[512];
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bot %s", config->bot_token);
        const char *headers[] = {auth_header, NULL};
        
        HttpRequest req = {
            .url = url,
            .method = "GET",
            .headers = headers,
            .timeout_sec = 10
        };
        
        HttpResponse *resp = http_request(&req);
        if (resp && resp->success) {
            printf("[Discord] Bot token validated\n");
            channel->connected = true;
        } else {
            fprintf(stderr, "[Discord] Failed to validate bot token\n");
        }
        
        if (resp) http_response_free(resp);
    } else {
        fprintf(stderr, "[Discord] No bot token or webhook configured\n");
    }
}

// 断开连接
static void discord_disconnect(ChannelInstance *channel) {
    printf("[Discord] Disconnecting channel '%s'\n", channel->name);
    channel->connected = false;
}

// 通过 webhook 发送消息
static bool discord_send_via_webhook(DiscordConfig *config, const char *message) {
    if (!config->webhook_url) return false;
    
    // 构建请求体
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "content", message);
    
    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    
    // 发送请求
    HttpResponse *resp = http_post(config->webhook_url, body_str);
    free(body_str);
    
    if (!resp) return false;
    
    bool success = resp->success && resp->status_code == 204;
    http_response_free(resp);
    
    return success;
}

// 通过 API 发送消息
static bool discord_send_via_api(DiscordConfig *config, const char *message) {
    if (!config->bot_token || !config->channel_id) return false;
    
    // 构建请求URL
    char url[256];
    snprintf(url, sizeof(url), "%s/channels/%s/messages", DISCORD_API_BASE, config->channel_id);
    
    // 构建请求头
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bot %s", config->bot_token);
    const char *headers[] = {auth_header, "Content-Type: application/json", NULL};
    
    // 构建请求体
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "content", message);
    
    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    
    // 发送请求
    HttpRequest req = {
        .url = url,
        .method = "POST",
        .body = body_str,
        .content_type = "application/json",
        .headers = headers,
        .timeout_sec = 30
    };
    
    HttpResponse *resp = http_request(&req);
    free(body_str);
    
    if (!resp) return false;
    
    bool success = resp->success && resp->status_code == 200;
    http_response_free(resp);
    
    return success;
}

// 发送消息
static bool discord_send_message(ChannelInstance *channel, const char *message) {
    printf("[Discord] Sending message via channel '%s'\n", channel->name);
    
    DiscordConfig *config = (DiscordConfig *)channel->config;
    if (!config) {
        fprintf(stderr, "[Discord] No config\n");
        return false;
    }
    
    // 优先使用 webhook 模式
    if (config->webhook_url) {
        return discord_send_via_webhook(config, message);
    }
    
    // 使用 API 模式
    return discord_send_via_api(config, message);
}

// 初始化 Discord 渠道
void discord_channel_init(ChannelInstance *channel, ChannelConfig *base_config) {
    // 创建 Discord 特定配置
    DiscordConfig *config = (DiscordConfig *)calloc(1, sizeof(DiscordConfig));
    if (!config) {
        fprintf(stderr, "[Discord] Memory allocation failed\n");
        return;
    }
    
    // 从基础配置复制值
    if (base_config) {
        if (base_config->bot_token) config->bot_token = strdup(base_config->bot_token);
        else if (base_config->api_key) config->bot_token = strdup(base_config->api_key);
        if (base_config->channel_id) config->channel_id = strdup(base_config->channel_id);
        if (base_config->webhook_url) config->webhook_url = strdup(base_config->webhook_url);
    }
    
    // 设置渠道属性
    channel->config = config;
    channel->connect = discord_connect;
    channel->disconnect = discord_disconnect;
    channel->send_message = discord_send_message;
    channel->receive_message = NULL;  // 使用默认消息处理流程
    channel->cleanup = discord_cleanup;
    
    printf("[Discord] Channel '%s' initialized\n", channel->name);
}

// 设置 Discord channel ID
void discord_set_channel_id(ChannelInstance *channel, const char *channel_id) {
    DiscordConfig *config = (DiscordConfig *)channel->config;
    if (!config) return;
    
    free(config->channel_id);
    config->channel_id = channel_id ? strdup(channel_id) : NULL;
    
    printf("[Discord] Channel ID set to: %s\n", channel_id);
}
