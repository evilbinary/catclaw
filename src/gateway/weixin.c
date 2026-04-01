#include "weixin.h"
#include "common/http_client.h"
#include "common/cJSON.h"
#include "common/config.h"
#include "common/log.h"
#include "agent/command.h"
#include "agent/agent.h"
#include "channels.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <pthread.h>
#endif

// 生成随机 X-WECHAT-UIN (随机uint32转base64)
static char* generate_wechat_uin(void) {
    uint32_t random_val = (uint32_t)rand();
    char decimal_str[16];
    snprintf(decimal_str, sizeof(decimal_str), "%u", random_val);
    
    // base64编码
    static const char base64_chars[] = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    size_t input_len = strlen(decimal_str);
    size_t output_len = 4 * ((input_len + 2) / 3);
    char *encoded = malloc(output_len + 1);
    if (!encoded) return strdup("MA=="); // 回退到 "0"
    
    size_t i, j;
    for (i = 0, j = 0; i < input_len; ) {
        uint32_t octet_a = i < input_len ? decimal_str[i++] : 0;
        uint32_t octet_b = i < input_len ? decimal_str[i++] : 0;
        uint32_t octet_c = i < input_len ? decimal_str[i++] : 0;
        
        uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;
        
        encoded[j++] = base64_chars[(triple >> 18) & 0x3F];
        encoded[j++] = base64_chars[(triple >> 12) & 0x3F];
        encoded[j++] = base64_chars[(triple >> 6) & 0x3F];
        encoded[j++] = base64_chars[triple & 0x3F];
    }
    
    // 填充
    size_t mod = input_len % 3;
    if (mod > 0) {
        for (size_t k = 0; k < (3 - mod); k++) {
            encoded[output_len - 1 - k] = '=';
        }
    }
    encoded[output_len] = '\0';
    
    return encoded;
}

// 构建请求头
static HttpHeaders* build_weixin_headers(const char *bot_token);

// 前向声明
static bool weixin_send_message_ex(const char *bot_token, const char *to_user_id,
                                   const char *text, const char *context_token, int message_state);

// 构建请求头实现
static HttpHeaders* build_weixin_headers(const char *bot_token) {
    HttpHeaders *headers = http_headers_new();
    if (!headers) return NULL;
    
    http_headers_add(headers, "Content-Type", "application/json");
    http_headers_add(headers, "AuthorizationType", "ilink_bot_token");
    
    char *uin = generate_wechat_uin();
    http_headers_add(headers, "X-WECHAT-UIN", uin);
    free(uin);
    
    if (bot_token) {
        char auth_header[512];
        snprintf(auth_header, sizeof(auth_header), "Bearer %s", bot_token);
        http_headers_add(headers, "Authorization", auth_header);
    }
    
    return headers;
}

// 获取二维码
bool weixin_get_qrcode(char **qrcode, char **qrcode_img) {
    if (!qrcode) return false;
    
    char url[256];
    snprintf(url, sizeof(url), "%s/ilink/bot/get_bot_qrcode?bot_type=3", 
             WEIXIN_ILINK_BASE_URL);
    
    log_info("[Weixin] Requesting QR code from: %s", url);
    
    HttpHeaders *headers = build_weixin_headers(NULL);
    if (!headers) {
        log_error("[Weixin] Failed to build headers");
        return false;
    }
    
    HttpResponse *resp = http_get_with_headers(url, headers);
    http_headers_free(headers);
    
    if (!resp) {
        log_error("[Weixin] HTTP request failed, no response");
        return false;
    }
    
    log_info("[Weixin] QR code response: status=%d, success=%d, body_len=%zu", 
             resp->status_code, resp->success, resp->body_len);
    
    if (!resp->success) {
        log_error("[Weixin] Failed to get QR code, status: %d", resp->status_code);
        if (resp->body) {
            log_error("[Weixin] Response body: %s", resp->body);
        }
        http_response_free(resp);
        return false;
    }
    
    if (!resp->body) {
        log_error("[Weixin] Empty response body");
        http_response_free(resp);
        return false;
    }
    
    log_debug("[Weixin] Response body: %s", resp->body);
    
    cJSON *json = cJSON_Parse(resp->body);
    http_response_free(resp);
    
    if (!json) {
        log_error("[Weixin] Failed to parse QR code response as JSON");
        return false;
    }
    
    cJSON *qrcode_item = cJSON_GetObjectItem(json, "qrcode");
    cJSON *img_item = cJSON_GetObjectItem(json, "qrcode_img_content");
    
    if (qrcode_item && cJSON_IsString(qrcode_item)) {
        *qrcode = strdup(qrcode_item->valuestring);
        log_info("[Weixin] Got qrcode: %s", *qrcode);
    } else {
        log_error("[Weixin] No qrcode field in response");
    }
    
    if (qrcode_img && img_item && cJSON_IsString(img_item)) {
        *qrcode_img = strdup(img_item->valuestring);
        log_info("[Weixin] Got qrcode_img, length: %zu", strlen(*qrcode_img));
    } else {
        log_error("[Weixin] No qrcode_img_content field in response");
    }
    
    cJSON_Delete(json);
    return (*qrcode != NULL);
}

// 检查二维码状态
bool weixin_check_qrcode_status(const char *qrcode, char **bot_token, char **base_url) {
    if (!qrcode) return false;
    
    char url[512];
    snprintf(url, sizeof(url), "%s/ilink/bot/get_qrcode_status?qrcode=%s",
             WEIXIN_ILINK_BASE_URL, qrcode);
    
    HttpHeaders *headers = build_weixin_headers(NULL);
    HttpResponse *resp = http_get_with_headers(url, headers);
    http_headers_free(headers);
    
    if (!resp || !resp->success) {
        log_error("[Weixin] Failed to check QR code status");
        if (resp) http_response_free(resp);
        return false;
    }
    
    cJSON *json = cJSON_Parse(resp->body);
    http_response_free(resp);
    
    if (!json) {
        log_error("[Weixin] Failed to parse status response");
        return false;
    }
    
    cJSON *status = cJSON_GetObjectItem(json, "status");
    bool confirmed = (status && cJSON_IsString(status) && 
                      strcmp(status->valuestring, "confirmed") == 0);
    
    if (confirmed) {
        if (bot_token) {
            cJSON *token = cJSON_GetObjectItem(json, "bot_token");
            if (token && cJSON_IsString(token)) {
                *bot_token = strdup(token->valuestring);
            }
        }
        if (base_url) {
            cJSON *base = cJSON_GetObjectItem(json, "baseurl");
            if (base && cJSON_IsString(base)) {
                *base_url = strdup(base->valuestring);
            }
        }
    }
    
    cJSON_Delete(json);
    return confirmed;
}

// 获取更新（长轮询）
bool weixin_get_updates(const char *bot_token, const char *cursor,
                        WeixinMessage **messages, int *msg_count, char **new_cursor) {
    if (!bot_token) return false;
    
    char url[256];
    snprintf(url, sizeof(url), "%s/ilink/bot/getupdates", WEIXIN_ILINK_BASE_URL);
    
    // 构建请求体
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "get_updates_buf", cursor ? cursor : "");
    
    cJSON *base_info = cJSON_CreateObject();
    cJSON_AddStringToObject(base_info, "channel_version", "1.0.2");
    cJSON_AddItemToObject(root, "base_info", base_info);
    
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    if (!body) return false;
    
    HttpHeaders *headers = build_weixin_headers(bot_token);
    HttpResponse *resp = http_post_json_with_headers(url, body, headers);
    http_headers_free(headers);
    free(body);
    
    if (!resp || !resp->success) {
        log_error("[Weixin] Failed to get updates");
        if (resp) http_response_free(resp);
        return false;
    }
    // 保存响应体用于错误日志（在释放前）
    char *resp_body_copy = strdup(resp->body);
    cJSON *json = cJSON_Parse(resp->body);
    http_response_free(resp);
    
    if (!json) {
        log_error("[Weixin] Failed to parse updates response: %s", resp_body_copy);
        free(resp_body_copy);
        return false;
    }
    
    cJSON *errcode = cJSON_GetObjectItem(json, "errcode");
    int code = errcode ? errcode->valueint : 0;
    
    if (code < 0) {
        log_error("[Weixin] Get updates response: %s", resp_body_copy);
        cJSON_Delete(json);
        free(resp_body_copy);
        // errcode -14 表示 session timeout，需要重新登录
        // 返回 false，调用方应该检测到此错误并触发重新登录
        return false;
    }
    free(resp_body_copy);
    
    // 更新游标
    if (new_cursor) {
        cJSON *buf = cJSON_GetObjectItem(json, "get_updates_buf");
        if (buf && cJSON_IsString(buf)) {
            *new_cursor = strdup(buf->valuestring);
        }
    }
    
    // 解析消息
    cJSON *msgs = cJSON_GetObjectItem(json, "msgs");
    if (msgs && cJSON_IsArray(msgs) && msg_count) {
        *msg_count = cJSON_GetArraySize(msgs);
        if (messages && *msg_count > 0) {
            *messages = calloc(*msg_count, sizeof(WeixinMessage));
            for (int i = 0; i < *msg_count; i++) {
                cJSON *msg = cJSON_GetArrayItem(msgs, i);
                WeixinMessage *wm = &(*messages)[i];
                
                cJSON *from = cJSON_GetObjectItem(msg, "from_user_id");
                if (from && cJSON_IsString(from)) {
                    wm->from_user_id = strdup(from->valuestring);
                }
                
                cJSON *to = cJSON_GetObjectItem(msg, "to_user_id");
                if (to && cJSON_IsString(to)) {
                    wm->to_user_id = strdup(to->valuestring);
                }
                
                cJSON *msg_type = cJSON_GetObjectItem(msg, "message_type");
                if (msg_type && cJSON_IsNumber(msg_type)) {
                    wm->message_type = msg_type->valueint;
                }
                
                cJSON *msg_state = cJSON_GetObjectItem(msg, "message_state");
                if (msg_state && cJSON_IsNumber(msg_state)) {
                    wm->message_state = msg_state->valueint;
                }
                
                cJSON *ctx_token = cJSON_GetObjectItem(msg, "context_token");
                if (ctx_token && cJSON_IsString(ctx_token)) {
                    wm->context_token = strdup(ctx_token->valuestring);
                }
                
                // 解析item_list获取文本内容
                cJSON *item_list = cJSON_GetObjectItem(msg, "item_list");
                if (item_list && cJSON_IsArray(item_list)) {
                    cJSON *first_item = cJSON_GetArrayItem(item_list, 0);
                    if (first_item) {
                        cJSON *type = cJSON_GetObjectItem(first_item, "type");
                        if (type && cJSON_IsNumber(type)) {
                            wm->item_type = type->valueint;
                        }
                        
                        cJSON *text_item = cJSON_GetObjectItem(first_item, "text_item");
                        if (text_item) {
                            cJSON *text = cJSON_GetObjectItem(text_item, "text");
                            if (text && cJSON_IsString(text)) {
                                wm->text = strdup(text->valuestring);
                            }
                        }
                    }
                }
            }
        }
    }
    
    cJSON_Delete(json);
    return true;
}

// 发送消息（完整消息，默认 FINISH 状态）
bool weixin_send_message(const char *bot_token, const char *to_user_id,
                         const char *text, const char *context_token) {
    return weixin_send_message_ex(bot_token, to_user_id, text, context_token, 2);  // 2=FINISH
}

// 清理消息数组
void weixin_free_messages(WeixinMessage *messages, int count) {
    if (!messages) return;
    for (int i = 0; i < count; i++) {
        free(messages[i].from_user_id);
        free(messages[i].to_user_id);
        free(messages[i].context_token);
        free(messages[i].text);
    }
    free(messages);
}

// 将base64图片保存到文件
static bool save_base64_image(const char* base64_data, const char* filepath) {
    if (!base64_data || !filepath) return false;
    
    static const int decode_table[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };
    
    size_t input_len = strlen(base64_data);
    size_t output_len = input_len / 4 * 3;
    
    if (input_len > 0 && base64_data[input_len - 1] == '=') output_len--;
    if (input_len > 1 && base64_data[input_len - 2] == '=') output_len--;
    
    unsigned char* decoded = (unsigned char*)malloc(output_len);
    if (!decoded) return false;
    
    size_t i, j;
    for (i = 0, j = 0; i < input_len; ) {
        int a = (base64_data[i] == '=') ? 0 : decode_table[(unsigned char)base64_data[i]]; i++;
        int b = (base64_data[i] == '=') ? 0 : decode_table[(unsigned char)base64_data[i]]; i++;
        int c = (base64_data[i] == '=') ? 0 : decode_table[(unsigned char)base64_data[i]]; i++;
        int d = (base64_data[i] == '=') ? 0 : decode_table[(unsigned char)base64_data[i]]; i++;
        
        decoded[j++] = (a << 2) | (b >> 4);
        if (j < output_len) decoded[j++] = ((b & 0x0f) << 4) | (c >> 2);
        if (j < output_len) decoded[j++] = ((c & 0x03) << 6) | d;
    }
    
    FILE* fp = fopen(filepath, "wb");
    if (!fp) {
        free(decoded);
        return false;
    }
    
    fwrite(decoded, 1, output_len, fp);
    fclose(fp);
    free(decoded);
    
    return true;
}

// 长轮询工作线程
#ifdef _WIN32
static DWORD WINAPI weixin_polling_thread(LPVOID arg) {
#else
static void* weixin_polling_thread(void *arg) {
#endif
    ChannelInstance *channel = (ChannelInstance *)arg;
    WeixinConfig *config = (WeixinConfig *)channel->user_data;
    
    if (!config) {
        log_error("[Weixin] Polling thread: not configured");
        return 0;
    }
    
    log_info("[Weixin] Polling thread started");
    
    // 主循环：登录 -> 消息轮询 -> session timeout -> 重新登录
    while (true) {
        // 如果未登录，等待扫码登录
        if (!config->is_logged_in) {
            log_info("[Weixin] Waiting for QR code scan login...");
            
            // 获取二维码
            char* qrcode = NULL;
            char* qrcode_img = NULL;
            
            if (weixin_get_qrcode(&qrcode, &qrcode_img)) {
                log_info("[Weixin] QR code obtained: %s", qrcode);
                
                // 构建登录消息
                char login_msg[1024];
                if (qrcode_img && strncmp(qrcode_img, "http", 4) == 0) {
                    snprintf(login_msg, sizeof(login_msg), 
                        "📱 微信登录提醒\n\n"
                        "请用微信扫描二维码登录：\n%s\n\n"
                        "等待扫码中...", qrcode_img);
                } else {
                    snprintf(login_msg, sizeof(login_msg), 
                        "📱 微信登录提醒\n\n"
                        "请在控制台扫描二维码登录，或查看 weixin_qrcode.png 文件");
                }
                
                // 发送登录消息到其他已连接的 channel
                channel_send_message_to_all(login_msg);
                
                // 显示二维码
                if (qrcode_img) {
                    if (strncmp(qrcode_img, "http", 4) == 0) {
                        printf("\n========================================\n");
                        printf("微信登录二维码链接:\n");
                        printf("%s\n", qrcode_img);
                        printf("\n请用微信扫描二维码登录...\n");
                        printf("========================================\n\n");
                        log_info("[Weixin] QR code URL: %s", qrcode_img);
                    } else {
                        char qrcode_path[512];
                        snprintf(qrcode_path, sizeof(qrcode_path), "weixin_qrcode.png");
                        
                        if (save_base64_image(qrcode_img, qrcode_path)) {
                            printf("\n========================================\n");
                            printf("微信登录二维码已保存到: %s\n", qrcode_path);
                            printf("请用微信扫描二维码登录...\n");
                            printf("========================================\n\n");
                            log_info("[Weixin] QR code saved to: %s", qrcode_path);
                        }
                    }
                }
                
                printf("等待扫码中");
                fflush(stdout);
                
                // 轮询等待扫码
                char* bot_token = NULL;
                char* base_url = NULL;
                int max_wait = 120;
                int waited = 0;
                
                while (waited < max_wait) {
#ifdef _WIN32
                    Sleep(1000);
#else
                    sleep(1);
#endif
                    waited++;
                    
                    printf(".");
                    fflush(stdout);
                    
                    if (weixin_check_qrcode_status(qrcode, &bot_token, &base_url)) {
                        printf("\n\n========================================\n");
                        printf("微信登录成功！\n");
                        printf("========================================\n\n");
                        log_info("[Weixin] Login successful!");
                        
                        free(config->bot_token);  // 释放旧 token
                        config->bot_token = bot_token;
                        free(config->base_url);
                        config->base_url = base_url ? base_url : strdup(WEIXIN_ILINK_BASE_URL);
                        config->is_logged_in = true;
                        channel->connected = true;
                        
                        printf("Bot Token: %s\n", bot_token);
                        printf("请将此 token 保存到配置文件 ~/.catclaw/config.json 中:\n");
                        printf("  \"api_key\": \"%s\"\n\n", bot_token);
                        
                        // 发送登录成功消息到其他 channel
                        channel_send_message_to_all("✅ 微信登录成功！");
                        break;
                    }
                }
                
                if (!config->is_logged_in) {
                    printf("\n\n[Weixin] Login timeout, retrying...\n");
                    log_warn("[Weixin] Login timeout after %d seconds, retrying...", max_wait);
                    
                    // 发送超时消息
                    channel_send_message_to_all("⏰ 微信登录超时，正在重新获取二维码...");
                    
                    free(qrcode);
                    free(qrcode_img);
                    continue;  // 重新尝试登录
                }
                
                free(qrcode);
                free(qrcode_img);
            } else {
                log_error("[Weixin] Failed to get QR code, retrying in 5 seconds...");
#ifdef _WIN32
                Sleep(5000);
#else
                sleep(5);
#endif
                continue;  // 重新尝试登录
            }
        }
        
        // 开始消息轮询
        log_info("[Weixin] Starting message polling...");
        
        int consecutive_failures = 0;
        const int max_failures = 3;  // 连续失败3次后触发重新登录
        
        while (config->is_logged_in) {
            WeixinMessage *messages = NULL;
            int msg_count = 0;
            char *new_cursor = NULL;        
            if (weixin_get_updates(config->bot_token, config->get_updates_buf,
                                   &messages, &msg_count, &new_cursor)) {
                consecutive_failures = 0;  // 重置失败计数
                
                if (new_cursor) {
                    free(config->get_updates_buf);
                    config->get_updates_buf = new_cursor;
                }
                
                for (int i = 0; i < msg_count; i++) {
                    WeixinMessage *msg = &messages[i];
                    
                    log_debug("[Weixin] Processing msg %d: type=%d, from=%s, text=%s", 
                             i, msg->message_type, msg->from_user_id ? msg->from_user_id : "null",
                             msg->text ? msg->text : "null");
                    
                    if (msg->message_type == 1 && msg->text) {
                        log_info("[Weixin] Message from %s to %s: %s", 
                                 msg->from_user_id, msg->to_user_id, msg->text);
                        log_debug("[Weixin] context_token: %s", 
                                 msg->context_token ? msg->context_token : "null");
                        
                        // 预先创建流式上下文（保存正确的 context_token，防止被新消息覆盖）
                        if (channel->stream_ctx) {
                            WeixinStreamContext *old_ctx = (WeixinStreamContext *)channel->stream_ctx;
                            free(old_ctx->from_user_id);
                            free(old_ctx->context_token);
                            free(old_ctx->accumulated);
                            free(old_ctx);
                        }
                        WeixinStreamContext *ctx = (WeixinStreamContext *)calloc(1, sizeof(WeixinStreamContext));
                        ctx->from_user_id = msg->from_user_id ? strdup(msg->from_user_id) : NULL;
                        ctx->context_token = msg->context_token ? strdup(msg->context_token) : NULL;
                        ctx->accumulated = strdup("");
                        ctx->active = true;
                        channel->stream_ctx = ctx;
                        
                        ChannelIncomingMessage incoming = {
                            .content = msg->text,
                            .sender_id = msg->from_user_id,
                            .chat_id = msg->from_user_id,
                            .message_id = NULL,
                            .extra = msg
                        };
                        
                        char* response = NULL;
                        bool handled = channel_handle_incoming_message(channel, &incoming, &response);
                        
                        log_debug("[Weixin] handle_incoming_message: handled=%d, response=%s", 
                                 handled, response ? response : "null");
                        
                        if (handled && response) {
                            weixin_send_message(config->bot_token, msg->from_user_id,
                                               response, msg->context_token);
                            free(response);
                        }
                    }
                }
                
                weixin_free_messages(messages, msg_count);
            } else {
                consecutive_failures++;
                log_warn("[Weixin] Get updates failed (%d/%d)", consecutive_failures, max_failures);
                
                if (consecutive_failures >= max_failures) {
                    log_error("[Weixin] Session may have expired, triggering re-login...");
                    config->is_logged_in = false;
                    break;  // 退出轮询，重新进入登录流程
                }
            }
            
#ifdef _WIN32
            Sleep(100);
#else
            usleep(200000);
#endif
        }
        
        // 如果是 session timeout，继续循环重新登录
        // 如果程序退出，外部会设置 channel->connected = false
        if (!channel->connected) {
            break;  // 程序退出
        }
    }
    
    log_info("[Weixin] Polling thread stopped");
    return 0;
}

// 发送消息回调
static bool weixin_channel_send_message(ChannelInstance *channel, const char *message) {
    // 微信channel需要通过context_token发送，这里简化处理
    log_info("[Weixin] Send message: %s", message);
    return true;
}

// ==================== 流式消息回调 ====================

// 发送消息（支持流式状态）
// message_state: 1=WRITING（正在输入）, 2=FINISH（完成）
static bool weixin_send_message_ex(const char *bot_token, const char *to_user_id,
                                   const char *text, const char *context_token, int message_state) {
    if (!bot_token || !to_user_id || !text) {
        log_error("[Weixin] send_message_ex: invalid params");
        return false;
    }
    
    log_info("[Weixin] Sending message to %s (state=%d): %s", to_user_id, message_state, text);
    log_debug("[Weixin] context_token: %s", context_token ? context_token : "null");
    
    char url[256];
    snprintf(url, sizeof(url), "%s/ilink/bot/sendmessage", WEIXIN_ILINK_BASE_URL);
    
    // 生成 client_id (时间戳 + 随机数)
    char client_id[64];
    snprintf(client_id, sizeof(client_id), "catclaw:%ld%04d", 
             (long)time(NULL), rand() % 10000);
    
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "from_user_id", "");  // 空字符串
    cJSON_AddStringToObject(msg, "to_user_id", to_user_id);
    cJSON_AddStringToObject(msg, "client_id", client_id);
    cJSON_AddNumberToObject(msg, "message_type", 2);  // BOT
    cJSON_AddNumberToObject(msg, "message_state", message_state);  // 2=FINISH
    
    if (context_token) {
        cJSON_AddStringToObject(msg, "context_token", context_token);
    }
    
    cJSON *item_list = cJSON_CreateArray();
    cJSON *item = cJSON_CreateObject();
    cJSON_AddNumberToObject(item, "type", 1);  // TEXT
    
    cJSON *text_item = cJSON_CreateObject();
    cJSON_AddStringToObject(text_item, "text", text);
    cJSON_AddItemToObject(item, "text_item", text_item);
    
    cJSON_AddItemToArray(item_list, item);
    cJSON_AddItemToObject(msg, "item_list", item_list);
    
    // 构建完整请求体
    cJSON *root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "msg", msg);
    
    // 添加 base_info (必须!)
    cJSON *base_info = cJSON_CreateObject();
    cJSON_AddStringToObject(base_info, "channel_version", "1.0.2");
    cJSON_AddItemToObject(root, "base_info", base_info);
    
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    if (!body) return false;
    
    log_debug("[Weixin] Send message request body: %s", body);
    
    HttpHeaders *headers = build_weixin_headers(bot_token);
    HttpResponse *resp = http_post_json_with_headers(url, body, headers);
    http_headers_free(headers);

    log_info("[Weixin] Send response: status=%d, success=%d, body=%s", 
        resp ? resp->status_code : 0, resp ? resp->success : 0, resp && resp->body ? resp->body : "null");

        
    free(body);
    
    if (!resp) {
        log_error("[Weixin] Send message: no response");
        return false;
    }
    
   
    bool success = resp->success;
    http_response_free(resp);
    return success;
}

// 发送"正在输入"状态 (需要先调用 getconfig 获取 typing_ticket，暂时禁用)
static bool weixin_send_typing(const char *bot_token, const char *to_user_id, const char *context_token) {
    // 根据协议，sendtyping 需要 typing_ticket，需要先调用 getconfig 获取
    // 暂时禁用此功能，直接返回成功
    (void)bot_token;
    (void)to_user_id;
    (void)context_token;
    log_debug("[Weixin] Typing indicator disabled (requires typing_ticket)");
    return true;
}

// 开始流式消息
static bool weixin_stream_start_callback(ChannelInstance *channel, const char *initial_content) {
    WeixinConfig *config = (WeixinConfig *)channel->user_data;
    if (!config || !config->bot_token) {
        log_error("[Weixin] Stream start: missing config");
        return false;
    }
    
    // 使用已创建的流式上下文（在收到消息时已创建并保存了正确的 context_token）
    WeixinStreamContext *ctx = (WeixinStreamContext *)channel->stream_ctx;
    if (!ctx || !ctx->from_user_id) {
        log_error("[Weixin] Stream start: no active stream context");
        return false;
    }
    
    log_info("[Weixin] Stream started for user: %s", ctx->from_user_id);
    
    // 发送"正在输入"状态
    weixin_send_typing(config->bot_token, ctx->from_user_id, ctx->context_token);
    
    // 累积初始内容
    if (initial_content && strlen(initial_content) > 0) {
        free(ctx->accumulated);
        ctx->accumulated = strdup(initial_content);
    }
    
    return true;
}

// 更新流式消息（只累积内容，不发送）
static bool weixin_stream_update_callback(ChannelInstance *channel, const char *content) {
    WeixinConfig *config = (WeixinConfig *)channel->user_data;
    if (!config || !channel->stream_ctx) {
        log_error("[Weixin] Stream update: no active stream");
        return false;
    }
    
    WeixinStreamContext *ctx = (WeixinStreamContext *)channel->stream_ctx;
    if (!ctx->active) {
        log_error("[Weixin] Stream not active");
        return false;
    }
    
    // 注意：content 参数已经是完整的累积内容，直接替换而不是追加
    if (content && strlen(content) > 0) {
        free(ctx->accumulated);
        ctx->accumulated = strdup(content);
    }
    
    log_debug("[Weixin] Stream update: accumulated %zu chars", 
             ctx->accumulated ? strlen(ctx->accumulated) : 0);
    
    return true;
}

// 结束流式消息（发送最终版本）
static bool weixin_stream_end_callback(ChannelInstance *channel) {
    WeixinConfig *config = (WeixinConfig *)channel->user_data;
    if (!config || !channel->stream_ctx) {
        return true;
    }
    
    WeixinStreamContext *ctx = (WeixinStreamContext *)channel->stream_ctx;
    bool success = true;
    
    // 发送最终消息（FINISH 状态）
    if (ctx->active && ctx->accumulated && strlen(ctx->accumulated) > 0) {
        log_info("[Weixin] Stream end: sending final message (%zu chars)", 
                strlen(ctx->accumulated));
        success = weixin_send_message_ex(config->bot_token, ctx->from_user_id,
                                        ctx->accumulated, ctx->context_token, 2);  // 2=FINISH
    }
    
    // 清理上下文
    free(ctx->from_user_id);
    free(ctx->context_token);
    free(ctx->accumulated);
    free(ctx);
    channel->stream_ctx = NULL;
    
    return success;
}

// 清理channel
static void weixin_channel_cleanup(ChannelInstance *channel) {
    channel->connected = false;
    
    if (channel->user_data) {
        WeixinConfig *config = (WeixinConfig *)channel->user_data;
        free(config->bot_token);
        free(config->base_url);
        free(config->get_updates_buf);
        free(config);
        channel->user_data = NULL;
    }
    
    // 清理流式上下文
    if (channel->stream_ctx) {
        WeixinStreamContext *ctx = (WeixinStreamContext *)channel->stream_ctx;
        free(ctx->from_user_id);
        free(ctx->context_token);
        free(ctx->accumulated);
        free(ctx);
        channel->stream_ctx = NULL;
    }
    
    log_info("[Weixin] Channel cleaned up");
}

// 初始化微信channel
bool weixin_channel_init(ChannelInstance *channel, ChannelConfig *config) {
    if (!channel) {
        log_error("[Weixin] Invalid channel");
        return false;
    }
    
    log_info("[Weixin] Initializing channel...");
    
    WeixinConfig *weixin_config = (WeixinConfig *)calloc(1, sizeof(WeixinConfig));
    if (!weixin_config) {
        log_error("[Weixin] Memory allocation failed");
        return false;
    }
    
    // 从配置读取token（如果已登录）
    if (config && config->api_key && strlen(config->api_key) > 0) {
        // bot_token 是扫码登录后返回的完整字符串，直接使用
        weixin_config->bot_token = strdup(config->api_key);
        weixin_config->is_logged_in = true;
        channel->connected = true;
        // log_debug("[Weixin] Using bot_token: %s", weixin_config->bot_token);
    }
    
    weixin_config->base_url = strdup(WEIXIN_ILINK_BASE_URL);
    weixin_config->stream_mode = config ? config->stream_mode : false;
    
    channel->user_data = weixin_config;
    channel->send_message = weixin_channel_send_message;
    channel->cleanup = weixin_channel_cleanup;
    channel->stream_ctx = NULL;
    
    // 设置流式消息回调（用于 AI 流式响应）
    channel->stream_start = weixin_stream_start_callback;
    channel->stream_update = weixin_stream_update_callback;
    channel->stream_end = weixin_stream_end_callback;
    
    // 启动轮询线程（线程内会处理登录流程）
#ifdef _WIN32
    HANDLE thread = CreateThread(NULL, 0, weixin_polling_thread, channel, 0, NULL);
    if (thread) CloseHandle(thread);
#else
    pthread_t thread;
    pthread_create(&thread, NULL, weixin_polling_thread, channel);
    pthread_detach(thread);
#endif
    
    log_info("[Weixin] Channel initialized");
    return true;
}
