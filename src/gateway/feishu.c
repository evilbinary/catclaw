#include "feishu.h"
#include "common/http_client.h"
#include "common/cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// 飞书配置结构
typedef struct {
    char *app_id;           // 应用ID
    char *app_secret;       // 应用密钥
    char *webhook_url;      // 机器人webhook地址 (简单模式)
    char *receive_id;       // 接收消息的用户/群组ID
    char *receive_id_type;  // 接收ID类型: open_id, user_id, union_id, chat_id
    char *access_token;     // 缓存的access_token
    time_t token_expire;    // token过期时间
} FeishuConfig;

// API 基础URL
#define FEISHU_API_BASE "https://open.feishu.cn/open-apis"

// 清理飞书配置
static void feishu_config_cleanup(FeishuConfig *config) {
    if (!config) return;
    free(config->app_id);
    free(config->app_secret);
    free(config->webhook_url);
    free(config->receive_id);
    free(config->receive_id_type);
    free(config->access_token);
    free(config);
}

// 渠道清理函数
static void feishu_cleanup(ChannelInstance *channel) {
    if (channel->config) {
        feishu_config_cleanup((FeishuConfig *)channel->config);
        channel->config = NULL;
    }
}

// 获取 tenant_access_token
char* feishu_get_tenant_access_token(const char *app_id, const char *app_secret) {
    if (!app_id || !app_secret) return NULL;
    
    // 构建请求URL
    char url[256];
    snprintf(url, sizeof(url), "%s/auth/v3/tenant_access_token/internal", FEISHU_API_BASE);
    
    // 构建请求体
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "app_id", app_id);
    cJSON_AddStringToObject(body, "app_secret", app_secret);
    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    
    // 发送请求
    HttpResponse *resp = http_post(url, body_str);
    free(body_str);
    
    if (!resp || !resp->success) {
        if (resp) http_response_free(resp);
        return NULL;
    }
    
    // 解析响应
    cJSON *json = cJSON_Parse(resp->body);
    http_response_free(resp);
    
    if (!json) return NULL;
    
    cJSON *token = cJSON_GetObjectItem(json, "tenant_access_token");
    char *result = token && cJSON_IsString(token) ? strdup(token->valuestring) : NULL;
    cJSON_Delete(json);
    
    return result;
}

// 获取有效的 access_token
static char* feishu_get_valid_token(FeishuConfig *config) {
    if (!config->app_id || !config->app_secret) {
        // 使用 webhook 模式不需要 token
        return NULL;
    }
    
    time_t now = time(NULL);
    
    // 如果 token 还有 60 秒以上有效期，直接返回
    if (config->access_token && config->token_expire > now + 60) {
        return config->access_token;
    }
    
    // 获取新 token
    free(config->access_token);
    config->access_token = feishu_get_tenant_access_token(config->app_id, config->app_secret);
    
    if (config->access_token) {
        config->token_expire = now + 7200; // token 有效期 2 小时
    }
    
    return config->access_token;
}

// 通过 webhook 发送消息
static bool feishu_send_via_webhook(FeishuConfig *config, const char *message) {
    if (!config->webhook_url) return false;
    
    // 构建消息体
    cJSON *body = cJSON_CreateObject();
    cJSON *content = cJSON_CreateObject();
    cJSON_AddStringToObject(content, "text", message);
    cJSON_AddStringToObject(body, "msg_type", "text");
    cJSON_AddItemToObject(body, "content", content);
    
    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    
    // 发送请求
    HttpResponse *resp = http_post(config->webhook_url, body_str);
    free(body_str);
    
    if (!resp) return false;
    
    bool success = resp->success && resp->status_code == 200;
    http_response_free(resp);
    
    return success;
}

// 通过 API 发送消息
static bool feishu_send_via_api(FeishuConfig *config, const char *message) {
    char *token = feishu_get_valid_token(config);
    if (!token) return false;
    
    // 构建请求URL
    char url[256];
    snprintf(url, sizeof(url), "%s/im/v1/messages?receive_id_type=%s",
             FEISHU_API_BASE, config->receive_id_type ? config->receive_id_type : "open_id");
    
    // 构建请求体
    cJSON *body = cJSON_CreateObject();
    cJSON *content = cJSON_CreateObject();
    cJSON_AddStringToObject(content, "text", message);
    char *content_str = cJSON_PrintUnformatted(content);
    cJSON_Delete(content);
    
    cJSON_AddStringToObject(body, "receive_id", config->receive_id);
    cJSON_AddStringToObject(body, "msg_type", "text");
    cJSON_AddStringToObject(body, "content", content_str);
    free(content_str);
    
    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    
    // 构建请求头
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", token);
    const char *headers[] = {auth_header, "Content-Type: application/json", NULL};
    
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

// 连接飞书
static void feishu_connect(ChannelInstance *channel) {
    printf("[Feishu] Connecting channel '%s'\n", channel->name);
    
    FeishuConfig *config = (FeishuConfig *)channel->config;
    if (!config) {
        fprintf(stderr, "[Feishu] No config for channel '%s'\n", channel->name);
        return;
    }
    
    // 检查配置
    if (config->webhook_url) {
        printf("[Feishu] Using webhook mode\n");
        channel->connected = true;
    } else if (config->app_id && config->app_secret && config->receive_id) {
        printf("[Feishu] Using API mode (app_id: %s)\n", config->app_id);
        // 尝试获取 token 验证配置
        char *token = feishu_get_valid_token(config);
        if (token) {
            printf("[Feishu] Successfully obtained access token\n");
            channel->connected = true;
        } else {
            fprintf(stderr, "[Feishu] Failed to obtain access token\n");
        }
    } else {
        fprintf(stderr, "[Feishu] Incomplete configuration\n");
        fprintf(stderr, "[Feishu] Need either webhook_url or (app_id + app_secret + receive_id)\n");
    }
}

// 断开连接
static void feishu_disconnect(ChannelInstance *channel) {
    printf("[Feishu] Disconnecting channel '%s'\n", channel->name);
    
    FeishuConfig *config = (FeishuConfig *)channel->config;
    if (config) {
        // 清除缓存的 token
        free(config->access_token);
        config->access_token = NULL;
        config->token_expire = 0;
    }
    
    channel->connected = false;
}

// 发送消息
static bool feishu_send_message(ChannelInstance *channel, const char *message) {
    printf("[Feishu] Sending message via channel '%s'\n", channel->name);
    
    FeishuConfig *config = (FeishuConfig *)channel->config;
    if (!config) {
        fprintf(stderr, "[Feishu] No config\n");
        return false;
    }
    
    // 优先使用 webhook 模式
    if (config->webhook_url) {
        return feishu_send_via_webhook(config, message);
    }
    
    // 使用 API 模式
    return feishu_send_via_api(config, message);
}

// 接收消息
static bool feishu_receive_message(ChannelInstance *channel, const char *message) {
    printf("[Feishu] Receiving message: %s\n", message);
    return true;
}

// 初始化飞书渠道
void feishu_channel_init(ChannelInstance *channel, ChannelConfig *base_config) {
    // 创建飞书特定配置
    FeishuConfig *config = (FeishuConfig *)calloc(1, sizeof(FeishuConfig));
    if (!config) {
        fprintf(stderr, "[Feishu] Memory allocation failed\n");
        return;
    }
    
    // 从基础配置复制值
    if (base_config) {
        if (base_config->app_id) config->app_id = strdup(base_config->app_id);
        if (base_config->app_secret) config->app_secret = strdup(base_config->app_secret);
        if (base_config->webhook_url) config->webhook_url = strdup(base_config->webhook_url);
        if (base_config->receive_id) config->receive_id = strdup(base_config->receive_id);
        if (base_config->receive_id_type) config->receive_id_type = strdup(base_config->receive_id_type);
        else config->receive_id_type = strdup("open_id");
    }
    
    // 设置渠道属性
    channel->config = config;
    channel->connect = feishu_connect;
    channel->disconnect = feishu_disconnect;
    channel->send_message = feishu_send_message;
    channel->receive_message = feishu_receive_message;
    channel->cleanup = feishu_cleanup;
    
    printf("[Feishu] Channel '%s' initialized\n", channel->name);
}

// 设置飞书接收ID
void feishu_set_receive_id(ChannelInstance *channel, const char *receive_id, const char *receive_id_type) {
    FeishuConfig *config = (FeishuConfig *)channel->config;
    if (!config) return;
    
    free(config->receive_id);
    config->receive_id = receive_id ? strdup(receive_id) : NULL;
    
    free(config->receive_id_type);
    config->receive_id_type = receive_id_type ? strdup(receive_id_type) : strdup("open_id");
    
    printf("[Feishu] Receive ID set to: %s (type: %s)\n", receive_id, config->receive_id_type);
}
