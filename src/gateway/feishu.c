#include "feishu.h"
#include "common/http_client.h"
#include "common/cJSON.h"
#include "common/config.h"
#include "common/log.h"
#include "agent/agent.h"
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
    (void)channel;  // unused
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

// ==================== 事件处理 ====================

// 解析飞书消息事件
FeishuMessage* feishu_parse_message(const char *event_json) {
    if (!event_json) return NULL;
    
    cJSON *root = cJSON_Parse(event_json);
    if (!root) return NULL;
    
    FeishuMessage *msg = (FeishuMessage *)calloc(1, sizeof(FeishuMessage));
    if (!msg) {
        cJSON_Delete(root);
        return NULL;
    }
    
    // 解析事件结构
    cJSON *event = cJSON_GetObjectItem(root, "event");
    if (!event) {
        cJSON_Delete(root);
        free(msg);
        return NULL;
    }
    
    // 解析消息内容
    cJSON *message = cJSON_GetObjectItem(event, "message");
    if (message) {
        cJSON *message_id = cJSON_GetObjectItem(message, "message_id");
        if (message_id && cJSON_IsString(message_id)) {
            msg->message_id = strdup(message_id->valuestring);
        }
        
        cJSON *message_type = cJSON_GetObjectItem(message, "message_type");
        if (message_type && cJSON_IsString(message_type)) {
            msg->message_type = strdup(message_type->valuestring);
        }
        
        cJSON *chat_id = cJSON_GetObjectItem(message, "chat_id");
        if (chat_id && cJSON_IsString(chat_id)) {
            msg->chat_id = strdup(chat_id->valuestring);
        }
        
        // 解析消息内容 (JSON字符串)
        cJSON *content = cJSON_GetObjectItem(message, "content");
        if (content && cJSON_IsString(content)) {
            // content 是JSON字符串，需要解析获取text
            cJSON *content_json = cJSON_Parse(content->valuestring);
            if (content_json) {
                cJSON *text = cJSON_GetObjectItem(content_json, "text");
                if (text && cJSON_IsString(text)) {
                    msg->content = strdup(text->valuestring);
                }
                cJSON_Delete(content_json);
            }
            if (!msg->content) {
                // 如果解析失败，直接使用原始内容
                msg->content = strdup(content->valuestring);
            }
        }
    }
    
    // 解析发送者信息
    cJSON *sender = cJSON_GetObjectItem(event, "sender");
    if (sender) {
        cJSON *sender_id = cJSON_GetObjectItem(sender, "sender_id");
        if (sender_id) {
            cJSON *open_id = cJSON_GetObjectItem(sender_id, "open_id");
            if (open_id && cJSON_IsString(open_id)) {
                msg->sender_id = strdup(open_id->valuestring);
                msg->sender_type = strdup("open_id");
            } else {
                cJSON *user_id = cJSON_GetObjectItem(sender_id, "user_id");
                if (user_id && cJSON_IsString(user_id)) {
                    msg->sender_id = strdup(user_id->valuestring);
                    msg->sender_type = strdup("user_id");
                } else {
                    cJSON *union_id = cJSON_GetObjectItem(sender_id, "union_id");
                    if (union_id && cJSON_IsString(union_id)) {
                        msg->sender_id = strdup(union_id->valuestring);
                        msg->sender_type = strdup("union_id");
                    }
                }
            }
        }
    }
    
    cJSON_Delete(root);
    return msg;
}

// 释放消息结构
void feishu_message_free(FeishuMessage *msg) {
    if (!msg) return;
    free(msg->sender_id);
    free(msg->sender_type);
    free(msg->message_id);
    free(msg->message_type);
    free(msg->content);
    free(msg->chat_id);
    free(msg->channel_id);
    free(msg);
}

// 回复飞书消息
bool feishu_reply_message(const char *channel_id, const char *message_id, const char *content) {
    (void)message_id;  // unused for now
    
    if (!channel_id || !content) return false;
    
    // 查找对应的飞书渠道实例
    ChannelInstance *channel = channel_find(channel_id);
    if (!channel || channel->type != CHANNEL_FEISHU) {
        log_error("[Feishu] Channel not found or not a Feishu channel: %s", channel_id);
        return false;
    }
    
    // 使用渠道的发送消息功能
    return channel_send_message(channel, content);
}

// 处理飞书事件回调
char* feishu_handle_event(const char *body) {
    if (!body) {
        return strdup("{\"code\":400,\"msg\":\"Empty body\"}");
    }
    
    log_info("[Feishu] Received event: %.200s", body);
    
    cJSON *root = cJSON_Parse(body);
    if (!root) {
        return strdup("{\"code\":400,\"msg\":\"Invalid JSON\"}");
    }
    
    // 检查是否是URL验证请求
    cJSON *type = cJSON_GetObjectItem(root, "type");
    if (type && cJSON_IsString(type) && strcmp(type->valuestring, "url_verification") == 0) {
        cJSON *challenge = cJSON_GetObjectItem(root, "challenge");
        if (challenge && cJSON_IsString(challenge)) {
            char *response = (char *)malloc(strlen(challenge->valuestring) + 64);
            snprintf(response, strlen(challenge->valuestring) + 64,
                     "{\"challenge\":\"%s\"}", challenge->valuestring);
            cJSON_Delete(root);
            log_info("[Feishu] URL verification completed");
            return response;
        }
    }
    
    // 处理事件回调
    cJSON *schema = cJSON_GetObjectItem(root, "schema");
    if (schema && cJSON_IsString(schema) && strcmp(schema->valuestring, "2.0") == 0) {
        cJSON *header = cJSON_GetObjectItem(root, "header");
        if (header) {
            cJSON *event_type = cJSON_GetObjectItem(header, "event_type");
            if (event_type && cJSON_IsString(event_type)) {
                log_info("[Feishu] Event type: %s", event_type->valuestring);
                
                // 处理消息事件
                if (strncmp(event_type->valuestring, "im.message", 10) == 0) {
                    FeishuMessage *msg = feishu_parse_message(body);
                    if (msg && msg->content) {
                        log_info("[Feishu] Message from %s: %s", 
                                 msg->sender_id ? msg->sender_id : "unknown",
                                 msg->content);
                        
                        // 构建带有上下文的消息
                        // 格式: [feishu:sender_id:message_id] content
                        char *context_msg = (char *)malloc(512 + strlen(msg->content));
                        if (context_msg) {
                            snprintf(context_msg, 512 + strlen(msg->content),
                                     "[feishu:%s:%s] %s",
                                     msg->sender_id ? msg->sender_id : "unknown",
                                     msg->chat_id ? msg->chat_id : msg->message_id,
                                     msg->content);
                            
                            // 发送到 agent
                            if (agent_send_message(context_msg)) {
                                log_info("[Feishu] Message sent to agent successfully");
                            } else {
                                log_error("[Feishu] Failed to send message to agent");
                            }
                            free(context_msg);
                        }
                        
                        feishu_message_free(msg);
                    }
                }
            }
        }
    }
    
    cJSON_Delete(root);
    
    // 返回成功响应
    return strdup("{\"code\":0,\"msg\":\"success\"}");
}
