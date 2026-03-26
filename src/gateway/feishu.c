#include "feishu.h"
#include "common/http_client.h"
#include "common/cJSON.h"
#include "common/config.h"
#include "common/log.h"
#include "agent/command.h"
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
    bool stream_mode;       // 是否启用流式输出（打字机效果）
    int stream_speed;       // 流式输出速度（字符/秒）
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

// 判断消息是否包含 markdown 格式
static bool is_markdown_message(const char *message) {
    if (!message) return false;
    // 检查常见的 markdown 标记
    const char *md_patterns[] = {
        "**",        // 粗体
        "__",        // 粗体
        "*",         // 斜体
        "_",         // 斜体
        "~~",        // 删除线
        "`",         // 代码
        "```",       // 代码块
        "#",         // 标题
        "- ",        // 列表
        "* ",        // 列表
        "1. ",       // 有序列表
        "> ",        // 引用
        "[",         // 链接
        "!["         // 图片
    };
    for (int i = 0; i < sizeof(md_patterns) / sizeof(md_patterns[0]); i++) {
        if (strstr(message, md_patterns[i])) {
            return true;
        }
    }
    return false;
}

// 通过 webhook 发送消息
static bool feishu_send_via_webhook(FeishuConfig *config, const char *message) {
    if (!config->webhook_url) return false;
    
    // 打印原始消息
    log_info("[Feishu] Webhook sending message:\n%s", message);
    
    cJSON *body = cJSON_CreateObject();
    
    // 检测是否为 markdown 格式，使用 interactive 卡片发送
    if (is_markdown_message(message)) {
        log_info("[Feishu] Detected markdown, using interactive card with lark_md");
        // webhook 模式：直接把 card 对象放在消息体中
        cJSON *card = cJSON_CreateObject();
        cJSON *elements = cJSON_CreateArray();
        cJSON *element = cJSON_CreateObject();
        cJSON *text = cJSON_CreateObject();
        
        cJSON_AddStringToObject(text, "tag", "lark_md");
        cJSON_AddStringToObject(text, "content", message);
        
        cJSON_AddStringToObject(element, "tag", "div");
        cJSON_AddItemToObject(element, "text", text);
        cJSON_AddItemToArray(elements, element);
        
        cJSON_AddItemToObject(card, "elements", elements);
        cJSON_AddBoolToObject(card, "wide_screen_mode", true);
        
        // 添加 stream 模式配置（打字机效果）
        if (config->stream_mode) {
            cJSON_AddBoolToObject(card, "stream", true);
            log_info("[Feishu] Stream mode enabled for typewriter effect");
        }
        
        cJSON_AddStringToObject(body, "msg_type", "interactive");
        cJSON_AddItemToObject(body, "card", card);
    } else {
        log_info("[Feishu] Plain text message");
        // 普通文本消息
        cJSON *content = cJSON_CreateObject();
        cJSON_AddStringToObject(content, "text", message);
        cJSON_AddStringToObject(body, "msg_type", "text");
        cJSON_AddItemToObject(body, "content", content);
    }
    
    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    
    // 打印发送的 JSON
    log_info("[Feishu] Webhook request body:\n%s", body_str);
    
    // 发送请求
    HttpResponse *resp = http_post(config->webhook_url, body_str);
    free(body_str);
    
    if (!resp) {
        log_error("[Feishu] Webhook request failed: no response");
        return false;
    }
    
    // 打印响应内容
    log_info("[Feishu] Webhook response: status=%d, body=%s", 
             resp->status_code, resp->body ? resp->body : "(null)");
    
    bool success = resp->success && resp->status_code == 200;
    http_response_free(resp);
    
    return success;
}

// 通过 API 发送消息
static bool feishu_send_via_api(FeishuConfig *config, const char *message) {
    char *token = feishu_get_valid_token(config);
    if (!token) return false;
    
    // 打印原始消息
    log_info("[Feishu] API sending message:\n%s", message);
    
    // 构建请求URL
    char url[256];
    snprintf(url, sizeof(url), "%s/im/v1/messages?receive_id_type=%s",
             FEISHU_API_BASE, config->receive_id_type ? config->receive_id_type : "open_id");
    
    // 构建请求体
    cJSON *body = cJSON_CreateObject();
    char *content_str = NULL;
    const char *msg_type = "text";
    
    // 检测是否为 markdown 格式
    if (is_markdown_message(message)) {
        log_info("[Feishu] Detected markdown, using interactive card with lark_md");
        // 使用 interactive 卡片发送 markdown
        // content 字段是卡片 JSON 字符串（不是 {"card": ...}）
        cJSON *card = cJSON_CreateObject();
        cJSON *elements = cJSON_CreateArray();
        cJSON *element = cJSON_CreateObject();
        cJSON *text = cJSON_CreateObject();
        
        cJSON_AddStringToObject(text, "tag", "lark_md");
        cJSON_AddStringToObject(text, "content", message);
        
        cJSON_AddStringToObject(element, "tag", "div");
        cJSON_AddItemToObject(element, "text", text);
        cJSON_AddItemToArray(elements, element);
        
        cJSON_AddItemToObject(card, "elements", elements);
        cJSON_AddBoolToObject(card, "wide_screen_mode", true);
        
        // 添加 stream 模式配置（打字机效果）
        if (config->stream_mode) {
            cJSON_AddBoolToObject(card, "stream", true);
            log_info("[Feishu] Stream mode enabled for typewriter effect");
        }
        
        content_str = cJSON_PrintUnformatted(card);
        cJSON_Delete(card);
        msg_type = "interactive";
    } else {
        log_info("[Feishu] Plain text message");
        // 普通文本消息
        cJSON *content = cJSON_CreateObject();
        cJSON_AddStringToObject(content, "text", message);
        content_str = cJSON_PrintUnformatted(content);
        cJSON_Delete(content);
    }
    
    cJSON_AddStringToObject(body, "receive_id", config->receive_id);
    cJSON_AddStringToObject(body, "msg_type", msg_type);
    cJSON_AddStringToObject(body, "content", content_str);
    free(content_str);
    
    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    
    // 打印发送的 JSON
    log_info("[Feishu] API request body:\n%s", body_str);
    
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
    
    if (!resp) {
        log_error("[Feishu] API request failed: no response");
        return false;
    }
    
    // 打印响应内容
    log_info("[Feishu] API response: status=%d, body=%s", 
             resp->status_code, resp->body ? resp->body : "(null)");
    
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
    
    // 流式发送模式（仅支持 API 模式）
    if (config->stream_mode && config->app_id && config->app_secret && config->receive_id) {
        log_info("[Feishu] Using stream mode for typewriter effect");
        return feishu_stream_send(channel->id, message, config->stream_speed);
    }
    
    // 优先使用 webhook 模式
    if (config->webhook_url) {
        return feishu_send_via_webhook(config, message);
    }
    
    // 使用 API 模式
    return feishu_send_via_api(config, message);
}

// 流式发送消息 (打字机效果) - 完整消息一次性发送
static bool feishu_stream_send_callback(ChannelInstance *channel, const char *message) {
    FeishuConfig *config = (FeishuConfig *)channel->config;
    if (!config) {
        log_error("[Feishu] No config for stream send");
        return false;
    }
    
    // 流式发送需要 API 模式
    if (!config->app_id || !config->app_secret || !config->receive_id) {
        log_error("[Feishu] Stream mode requires API mode (app_id, app_secret, receive_id)");
        return false;
    }
    
    int speed = config->stream_speed > 0 ? config->stream_speed : 20;
    return feishu_stream_send(channel->id, message, speed);
}

// ==================== 实时流式消息回调 (配合 AI 流式输出) ====================

// 开始流式消息
static bool feishu_stream_start_callback(ChannelInstance *channel, const char *initial_content) {
    FeishuConfig *config = (FeishuConfig *)channel->config;
    if (!config || !config->app_id || !config->app_secret || !config->receive_id) {
        log_error("[Feishu] Stream start requires API mode");
        return false;
    }
    
    // 先结束之前的流式消息（如果有）
    if (channel->stream_ctx) {
        FeishuStreamContext *old_ctx = (FeishuStreamContext *)channel->stream_ctx;
        if (old_ctx->active && old_ctx->message_id) {
            feishu_stream_finish(channel->id, old_ctx->message_id);
        }
        free(old_ctx->message_id);
        free(old_ctx->channel_id);
        free(old_ctx->access_token);
        free(old_ctx->current_content);
        free(old_ctx);
        channel->stream_ctx = NULL;
    }
    
    // 创建新的流式消息
    char *message_id = feishu_stream_create(channel->id, config->receive_id, config->receive_id_type);
    if (!message_id) {
        log_error("[Feishu] Failed to create stream message");
        return false;
    }
    
    // 保存上下文
    FeishuStreamContext *ctx = (FeishuStreamContext *)calloc(1, sizeof(FeishuStreamContext));
    ctx->message_id = message_id;
    ctx->channel_id = strdup(channel->id);
    ctx->active = true;
    channel->stream_ctx = ctx;
    
    log_info("[Feishu] Stream started: message_id=%s", message_id);
    
    // 发送初始内容
    if (initial_content && strlen(initial_content) > 0) {
        if (!feishu_stream_update(channel->id, message_id, initial_content)) {
            log_error("[Feishu] Failed to send initial content");
            // 不返回 false，因为消息已创建，后续可以继续更新
        }
    }
    
    return true;
}

// 更新流式消息
static bool feishu_stream_update_callback(ChannelInstance *channel, const char *content) {
    if (!channel->stream_ctx) {
        log_error("[Feishu] No active stream context");
        return false;
    }
    
    FeishuStreamContext *ctx = (FeishuStreamContext *)channel->stream_ctx;
    if (!ctx->active || !ctx->message_id) {
        log_error("[Feishu] Stream not active");
        return false;
    }
    
    return feishu_stream_update(channel->id, ctx->message_id, content);
}

// 结束流式消息
static bool feishu_stream_end_callback(ChannelInstance *channel) {
    if (!channel->stream_ctx) {
        return true;  // 没有活跃的流式消息
    }
    
    FeishuStreamContext *ctx = (FeishuStreamContext *)channel->stream_ctx;
    bool success = true;
    
    if (ctx->active && ctx->message_id) {
        success = feishu_stream_finish(channel->id, ctx->message_id);
        log_info("[Feishu] Stream ended: message_id=%s", ctx->message_id);
    }
    
    // 清理上下文
    free(ctx->message_id);
    free(ctx->channel_id);
    free(ctx->access_token);
    free(ctx->current_content);
    free(ctx);
    channel->stream_ctx = NULL;
    
    return success;
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
        // 流式输出配置
        config->stream_mode = base_config->stream_mode;
        config->stream_speed = base_config->stream_speed > 0 ? base_config->stream_speed : 20;
    } else {
        config->stream_speed = 20; // 默认速度
    }
    
    // 设置渠道属性
    channel->config = config;
    channel->connect = feishu_connect;
    channel->disconnect = feishu_disconnect;
    channel->send_message = feishu_send_message;
    channel->receive_message = NULL;  // 使用默认消息处理流程
    channel->cleanup = feishu_cleanup;
    channel->stream_ctx = NULL;
    
    // 流式消息回调（API 模式下始终可用）
    if (config->app_id && config->app_secret && config->receive_id) {
        channel->stream_send = feishu_stream_send_callback;
        channel->stream_start = feishu_stream_start_callback;
        channel->stream_update = feishu_stream_update_callback;
        channel->stream_end = feishu_stream_end_callback;
    } else {
        channel->stream_send = NULL;
        channel->stream_start = NULL;
        channel->stream_update = NULL;
        channel->stream_end = NULL;
    }
    
    printf("[Feishu] Channel '%s' initialized (stream_mode=%s, speed=%d)\n", 
           channel->name, 
           config->stream_mode ? "enabled" : "disabled",
           config->stream_speed);
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

// 回复飞书消息（通过 chat_id/open_id）
bool feishu_reply_to_chat(const char *chat_id, const char *content) {
    if (!chat_id || !content) return false;
    
    // 获取第一个飞书渠道
    ChannelInstance *channel = channel_first_of_type(CHANNEL_FEISHU);
    if (!channel) {
        log_error("[Feishu] No Feishu channel available");
        return false;
    }
    
    FeishuConfig *config = (FeishuConfig *)channel->config;
    if (!config) {
        log_error("[Feishu] No config in channel");
        return false;
    }
    
    // 临时保存原来的 receive_id
    char *orig_receive_id = config->receive_id;
    char *orig_receive_id_type = config->receive_id_type;
    
    // 设置目标 chat_id
    config->receive_id = strdup(chat_id);
    config->receive_id_type = strdup("chat_id");  // 使用 chat_id 类型
    
    // 发送消息
    bool success = false;
    if (config->app_id && config->app_secret) {
        success = feishu_send_via_api(config, content);
    } else if (config->webhook_url) {
        success = feishu_send_via_webhook(config, content);
    }
    
    // 恢复原来的 receive_id
    free(config->receive_id);
    free(config->receive_id_type);
    config->receive_id = orig_receive_id;
    config->receive_id_type = orig_receive_id_type;
    
    return success;
}

// 回复飞书消息（保留向后兼容）
bool feishu_reply_message(const char *channel_id, const char *message_id, const char *content) {
    (void)message_id;  // unused for now
    
    // channel_id 可能是内部 channel_id 或者 Feishu chat_id
    // 先尝试查找内部 channel
    ChannelInstance *channel = channel_find(channel_id);
    if (channel && channel->type == CHANNEL_FEISHU) {
        return channel_send_message(channel, content);
    }
    
    // 不是内部 channel，可能是 Feishu chat_id，使用 reply_to_chat
    return feishu_reply_to_chat(channel_id, content);
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
                        
                        // 检查是否是命令（以 / 开头）
                        // 获取飞书 channel 实例
                        ChannelInstance* feishu_channel = channel_first_of_type(CHANNEL_FEISHU);

                        // 动态更新回复目标（用于流式回复）
                        const char* reply_target = msg->chat_id ? msg->chat_id : msg->sender_id;
                        if (reply_target && feishu_channel) {
                            feishu_set_receive_id(feishu_channel, reply_target, "chat_id");
                        }

                        // 构建统一消息结构
                        ChannelIncomingMessage incoming_msg = {
                            .content = msg->content,
                            .sender_id = msg->sender_id,
                            .chat_id = msg->chat_id,
                            .message_id = msg->message_id,
                            .extra = NULL
                        };

                        // 使用统一消息处理入口
                        char* response = NULL;
                        bool handled = channel_handle_incoming_message(feishu_channel, &incoming_msg, &response);

                        if (handled && response) {
                            // 已处理（命令或自定义处理），发送响应
                            const char* reply_id = msg->chat_id ? msg->chat_id : msg->sender_id;
                            if (reply_id) {
                                feishu_reply_to_chat(reply_id, response);
                            } else {
                                channel_send_message_to_type(CHANNEL_FEISHU, response);
                            }
                            free(response);
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

// ==================== 流式消息 API (打字机效果) ====================

// 创建流式消息 - 通过发送带stream属性的普通消息实现
char* feishu_stream_create(const char *channel_id, const char *receive_id, const char *receive_id_type) {
    if (!channel_id || !receive_id) return NULL;
    
    // 查找对应的飞书渠道实例
    ChannelInstance *channel = channel_find(channel_id);
    if (!channel || channel->type != CHANNEL_FEISHU) {
        log_error("[Feishu] Channel not found or not a Feishu channel: %s", channel_id);
        return NULL;
    }
    
    FeishuConfig *config = (FeishuConfig *)channel->config;
    if (!config) return NULL;
    
    char *token = feishu_get_valid_token(config);
    if (!token) {
        log_error("[Feishu] Failed to get access token for stream message");
        return NULL;
    }
    
    // 构建请求URL - 使用普通消息发送API
    char url[256];
    snprintf(url, sizeof(url), "%s/im/v1/messages?receive_id_type=%s",
             FEISHU_API_BASE, receive_id_type ? receive_id_type : "open_id");
    
    // 构建卡片 - 带stream属性实现流式输出
    cJSON *card = cJSON_CreateObject();
    cJSON *elements = cJSON_CreateArray();
    cJSON *element = cJSON_CreateObject();
    cJSON *text = cJSON_CreateObject();
    
    cJSON_AddStringToObject(text, "tag", "lark_md");
    cJSON_AddStringToObject(text, "content", " "); // 初始空内容
    cJSON_AddStringToObject(element, "tag", "div");
    cJSON_AddItemToObject(element, "text", text);
    cJSON_AddItemToArray(elements, element);
    cJSON_AddItemToObject(card, "elements", elements);
    cJSON_AddBoolToObject(card, "stream", true);  // 启用流式输出
    cJSON_AddBoolToObject(card, "scrollable", true);  // 启用滚动
    
    char *content_str = cJSON_PrintUnformatted(card);
    cJSON_Delete(card);
    
    // 构建请求体
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "receive_id", receive_id);
    cJSON_AddStringToObject(body, "msg_type", "interactive");
    cJSON_AddStringToObject(body, "content", content_str);
    free(content_str);
    
    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    
    log_info("[Feishu] Creating stream message with body: %s", body_str);
    
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
    
    if (!resp) {
        log_error("[Feishu] Stream create request failed: no response");
        return NULL;
    }
    
    log_debug("[Feishu] Stream create response: status=%d, body=%s", 
             resp->status_code, resp->body ? resp->body : "(null)");
    
    // 解析响应获取 message_id
    char *message_id = NULL;
    if (resp->success && resp->body) {
        cJSON *root = cJSON_Parse(resp->body);
        if (root) {
            cJSON *data = cJSON_GetObjectItem(root, "data");
            if (data) {
                cJSON *msg_id = cJSON_GetObjectItem(data, "message_id");
                if (msg_id && cJSON_IsString(msg_id)) {
                    message_id = strdup(msg_id->valuestring);
                    log_info("[Feishu] Stream message created: %s", message_id);
                }
            }
            cJSON_Delete(root);
        }
    }
    
    http_response_free(resp);
    return message_id;
}

// 更新流式消息内容 - 使用消息patch API
bool feishu_stream_update(const char *channel_id, const char *message_id, const char *content) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    if (!channel_id || !message_id || !content) return false;
    
    // 查找对应的飞书渠道实例
    ChannelInstance *channel = channel_find(channel_id);
    if (!channel || channel->type != CHANNEL_FEISHU) {
        return false;
    }
    
    FeishuConfig *config = (FeishuConfig *)channel->config;
    if (!config) return false;
    
    char *token = feishu_get_valid_token(config);
    if (!token) return false;
    
    // 保存当前内容到 context（用于结束流式时发送）
    if (channel->stream_ctx) {
        FeishuStreamContext *ctx = (FeishuStreamContext *)channel->stream_ctx;
        free(ctx->current_content);
        ctx->current_content = strdup(content);
    }
    
    // 构建请求URL - 使用消息patch API
    char url[256];
    snprintf(url, sizeof(url), "%s/im/v1/messages/%s", FEISHU_API_BASE, message_id);
    
    // 构建卡片内容
    cJSON *card = cJSON_CreateObject();
    cJSON *elements = cJSON_CreateArray();
    cJSON *element = cJSON_CreateObject();
    cJSON *text = cJSON_CreateObject();
    
    cJSON_AddStringToObject(text, "tag", "lark_md");
    cJSON_AddStringToObject(text, "content", content);
    cJSON_AddStringToObject(element, "tag", "div");
    cJSON_AddItemToObject(element, "text", text);
    cJSON_AddItemToArray(elements, element);
    cJSON_AddItemToObject(card, "elements", elements);
    cJSON_AddBoolToObject(card, "stream", true);  // 保持流式状态
    cJSON_AddBoolToObject(card, "scrollable", true);  // 启用滚动
    
    char *content_str = cJSON_PrintUnformatted(card);
    cJSON_Delete(card);
    
    // 构建请求体
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "content", content_str);
    free(content_str);
    
    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    
    // 构建请求头
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", token);
    const char *headers[] = {auth_header, "Content-Type: application/json", NULL};
    
    // 发送PATCH请求
    HttpRequest req = {
        .url = url,
        .method = "PATCH",
        .body = body_str,
        .content_type = "application/json",
        .headers = headers,
        .timeout_sec = 30
    };
    
    HttpResponse *resp = http_request(&req);
    free(body_str);

    clock_gettime(CLOCK_MONOTONIC, &end);
    long cost_ms = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000;
    log_debug("[TIMING] feishu HTTP: cost=%ldms, len=%zu", cost_ms, strlen(content));
    
    if (!resp) {
        log_error("[Feishu] Stream update request failed: no response");
        return false;
    }
    
    bool success = resp->success && resp->status_code == 200;
    if (!success) {
        log_error("[Feishu] Stream update failed: status=%d, body=%s", 
                  resp->status_code, resp->body ? resp->body : "(null)");
    }
    http_response_free(resp);
    
    return success;
}

// 结束流式消息 - 发送最终内容并取消stream属性
bool feishu_stream_finish(const char *channel_id, const char *message_id) {
    if (!channel_id || !message_id) return false;
    
    // 查找对应的飞书渠道实例
    ChannelInstance *channel = channel_find(channel_id);
    if (!channel || channel->type != CHANNEL_FEISHU) {
        return false;
    }
    
    FeishuConfig *config = (FeishuConfig *)channel->config;
    if (!config) return false;
    
    char *token = feishu_get_valid_token(config);
    if (!token) return false;
    
    // 获取保存的当前内容
    const char *final_content = " ";  // 默认空内容
    if (channel->stream_ctx) {
        FeishuStreamContext *ctx = (FeishuStreamContext *)channel->stream_ctx;
        if (ctx->current_content) {
            final_content = ctx->current_content;
        }
    }
    
    // 构建请求URL - 使用消息patch API
    char url[256];
    snprintf(url, sizeof(url), "%s/im/v1/messages/%s", FEISHU_API_BASE, message_id);
    
    // 构建卡片内容（最终内容）
    cJSON *card = cJSON_CreateObject();
    cJSON *elements = cJSON_CreateArray();
    cJSON *element = cJSON_CreateObject();
    cJSON *text = cJSON_CreateObject();
    
    cJSON_AddStringToObject(text, "tag", "lark_md");
    cJSON_AddStringToObject(text, "content", final_content);
    cJSON_AddStringToObject(element, "tag", "div");
    cJSON_AddItemToObject(element, "text", text);
    cJSON_AddItemToArray(elements, element);
    cJSON_AddItemToObject(card, "elements", elements);
    cJSON_AddBoolToObject(card, "stream", false);  // 结束流式
    cJSON_AddBoolToObject(card, "scrollable", true);  // 启用滚动
    
    char *content_str = cJSON_PrintUnformatted(card);
    cJSON_Delete(card);
    
    // 构建请求体
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "content", content_str);
    free(content_str);
    
    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    
    // 构建请求头
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", token);
    const char *headers[] = {auth_header, "Content-Type: application/json", NULL};
    
    // 发送PATCH请求
    HttpRequest req = {
        .url = url,
        .method = "PATCH",
        .body = body_str,
        .content_type = "application/json",
        .headers = headers,
        .timeout_sec = 30
    };
    
    HttpResponse *resp = http_request(&req);
    free(body_str);
    
    if (!resp) {
        log_error("[Feishu] Stream finish request failed: no response");
        return false;
    }
    
    log_info("[Feishu] Stream finished: message_id=%s, status=%d", message_id, resp->status_code);
    
    bool success = resp->success && resp->status_code == 200;
    if (!success) {
        log_error("[Feishu] Stream finish failed: body=%s", resp->body ? resp->body : "(null)");
    }
    http_response_free(resp);
    
    return success;
}

// 流式发送完整消息 (模拟打字机效果)

#ifdef _WIN32
#include <windows.h>
#define msleep(ms) Sleep(ms)
#else
#include <unistd.h>
#include <time.h>
#define msleep(ms) do { struct timespec ts = {0, (ms)*1000000L}; nanosleep(&ts, NULL); } while(0)
#endif

bool feishu_stream_send(const char *channel_id, const char *message, int speed_chars_per_sec) {
    if (!channel_id || !message) return false;
    
    // 查找对应的飞书渠道实例
    ChannelInstance *channel = channel_find(channel_id);
    if (!channel || channel->type != CHANNEL_FEISHU) {
        return false;
    }
    
    FeishuConfig *config = (FeishuConfig *)channel->config;
    if (!config) return false;
    
    // 创建流式消息
    char *message_id = feishu_stream_create(channel_id, config->receive_id, config->receive_id_type);
    if (!message_id) {
        log_error("[Feishu] Failed to create stream message");
        return false;
    }
    
    // 计算更新间隔 (微秒)
    int interval_us = 1000000 / (speed_chars_per_sec > 0 ? speed_chars_per_sec : 20);
    
    // 逐字更新内容
    int len = strlen(message);
    char *partial = (char *)malloc(len + 1);
    if (!partial) {
        free(message_id);
        return false;
    }
    
    // 分批更新，每20个字符更新一次
    int batch_size = 20;

    // 创建流式上下文，使 feishu_stream_update 能保存 current_content
    FeishuStreamContext stream_ctx = {0};
    stream_ctx.message_id = message_id;
    stream_ctx.channel_id = strdup(channel_id);
    stream_ctx.active = true;
    channel->stream_ctx = &stream_ctx;

    for (int i = 0; i < len; ) {
        int end = (i + batch_size < len) ? i + batch_size : len;
        // 对齐到 UTF-8 字符边界，避免截断多字节字符
        while (end > i && (message[end] & 0xC0) == 0x80) {
            end--;
        }
        if (end <= i) break;
        strncpy(partial, message, end);
        partial[end] = '\0';
        
        if (!feishu_stream_update(channel_id, message_id, partial)) {
            log_error("[Feishu] Failed to update stream message at position %d", i);
        }
        
        msleep(interval_us * (end - i) / 1000);
        i = end;
    }
    
    // 结束流式消息
    bool result = feishu_stream_finish(channel_id, message_id);
    channel->stream_ctx = NULL;
    free(stream_ctx.channel_id);
    free(stream_ctx.current_content);
    free(message_id);
    free(partial);
    
    return result;
}
