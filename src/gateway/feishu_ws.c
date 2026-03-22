/**
 * 飞书 WebSocket 客户端实现
 * 基于飞书 WebSocket 协议
 */
#include "feishu_ws.h"
#include "feishu.h"
#include "common/log.h"
#include "common/http_client.h"
#include "common/cJSON.h"
#include "agent/agent.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 飞书默认域名
#define FEISHU_DEFAULT_DOMAIN "https://open.feishu.cn"
#define FEISHU_ENDPOINT_URI "/callback/ws/endpoint"

// 消息类型
#define MSG_TYPE_PING "ping"
#define MSG_TYPE_PONG "pong"
#define MSG_TYPE_EVENT "event"
#define MSG_TYPE_CARD "card"

// Frame 类型
#define FRAME_TYPE_CONTROL 1
#define FRAME_TYPE_DATA     2

// ==================== Protobuf 编解码 ====================

// 写入 varint
static size_t pb_write_varint(uint8_t *buf, uint64_t val) {
    size_t len = 0;
    while (val > 127) {
        buf[len++] = (val & 0x7F) | 0x80;
        val >>= 7;
    }
    buf[len++] = val;
    return len;
}

// 读取 varint
static bool pb_read_varint(const uint8_t *buf, size_t buf_len, size_t *offset, uint64_t *val) {
    *val = 0;
    int shift = 0;
    while (*offset < buf_len) {
        uint8_t b = buf[(*offset)++];
        *val |= (uint64_t)(b & 0x7F) << shift;
        if ((b & 0x80) == 0) return true;
        shift += 7;
        if (shift >= 64) return false;
    }
    return false;
}

// 写入 tag (field_number << 3 | wire_type)
static size_t pb_write_tag(uint8_t *buf, int field_num, int wire_type) {
    return pb_write_varint(buf, (field_num << 3) | wire_type);
}

// 写入 string/bytes (length-delimited)
static size_t pb_write_string(uint8_t *buf, int field_num, const char *str, size_t str_len) {
    size_t len = pb_write_tag(buf, field_num, 2);  // wire_type=2
    len += pb_write_varint(buf + len, str_len);
    memcpy(buf + len, str, str_len);
    return len + str_len;
}

// 写入 varint field
static size_t pb_write_varint_field(uint8_t *buf, int field_num, uint64_t val) {
    size_t len = pb_write_tag(buf, field_num, 0);  // wire_type=0
    len += pb_write_varint(buf + len, val);
    return len;
}

// 读取 length-delimited
static bool pb_read_bytes(const uint8_t *buf, size_t buf_len, size_t *offset, 
                          uint8_t **data, size_t *data_len) {
    uint64_t len;
    if (!pb_read_varint(buf, buf_len, offset, &len)) return false;
    if (*offset + len > buf_len) return false;
    
    *data = (uint8_t *)malloc(len + 1);
    if (!*data) return false;
    memcpy(*data, buf + *offset, len);
    (*data)[len] = '\0';
    *data_len = len;
    *offset += len;
    return true;
}

// 读取 string
static bool pb_read_string(const uint8_t *buf, size_t buf_len, size_t *offset, char **str) {
    uint8_t *data;
    size_t data_len;
    if (!pb_read_bytes(buf, buf_len, offset, &data, &data_len)) return false;
    *str = (char *)data;
    return true;
}

// 解码 Header
typedef struct {
    char *key;
    char *value;
} PbHeader;

static void pb_header_free(PbHeader *h) {
    if (h) {
        free(h->key);
        free(h->value);
    }
}

static bool pb_decode_header(const uint8_t *buf, size_t buf_len, PbHeader *header) {
    memset(header, 0, sizeof(PbHeader));
    size_t offset = 0;
    
    while (offset < buf_len) {
        uint64_t tag;
        if (!pb_read_varint(buf, buf_len, &offset, &tag)) break;
        
        int field_num = tag >> 3;
        int wire_type = tag & 0x7;
        
        if (wire_type == 2) {  // length-delimited
            char *str = NULL;
            if (!pb_read_string(buf, buf_len, &offset, &str)) break;
            
            if (field_num == 1) {
                header->key = str;
            } else if (field_num == 2) {
                header->value = str;
            } else {
                free(str);
            }
        } else if (wire_type == 0) {  // varint
            uint64_t val;
            if (!pb_read_varint(buf, buf_len, &offset, &val)) break;
        } else {
            break;  // Unknown wire type
        }
    }
    
    return header->key && header->value;
}

// 解码 Frame
typedef struct {
    PbHeader *headers;
    int header_count;
    int64_t service_id;
    int method;
    int64_t seq_id;
    int64_t log_id;
    uint8_t *payload;
    size_t payload_len;
} PbFrame;

static void pb_frame_free(PbFrame *frame) {
    if (frame) {
        for (int i = 0; i < frame->header_count; i++) {
            pb_header_free(&frame->headers[i]);
        }
        free(frame->headers);
        free(frame->payload);
    }
}

static char* pb_frame_get_header(PbFrame *frame, const char *key) {
    for (int i = 0; i < frame->header_count; i++) {
        if (strcmp(frame->headers[i].key, key) == 0) {
            return frame->headers[i].value;
        }
    }
    return NULL;
}

static bool pb_decode_frame(const uint8_t *buf, size_t buf_len, PbFrame *frame) {
    memset(frame, 0, sizeof(PbFrame));
    size_t offset = 0;
    
    // Temporary storage for headers
    PbHeader headers[32];
    int header_count = 0;
    
    while (offset < buf_len) {
        uint64_t tag;
        size_t prev_offset = offset;
        if (!pb_read_varint(buf, buf_len, &offset, &tag)) break;
        
        int field_num = tag >> 3;
        int wire_type = tag & 0x7;
        
        if (wire_type == 2) {  // length-delimited
            if (field_num == 1) {  // headers (repeated)
                uint8_t *header_data;
                size_t header_len;
                size_t inner_offset = offset;
                if (!pb_read_bytes(buf, buf_len, &offset, &header_data, &header_len)) break;
                
                if (header_count < 32) {
                    if (pb_decode_header(header_data, header_len, &headers[header_count])) {
                        header_count++;
                    }
                }
                free(header_data);
            } else if (field_num == 2) {  // service (string)
                char *service_str;
                if (!pb_read_string(buf, buf_len, &offset, &service_str)) break;
                frame->service_id = atoll(service_str);
                free(service_str);
            } else if (field_num == 6) {  // payload
                if (!pb_read_bytes(buf, buf_len, &offset, &frame->payload, &frame->payload_len)) break;
            } else {
                // Skip unknown field
                uint64_t len;
                if (!pb_read_varint(buf, buf_len, &offset, &len)) break;
                offset += len;
            }
        } else if (wire_type == 0) {  // varint
            uint64_t val;
            if (!pb_read_varint(buf, buf_len, &offset, &val)) break;
            
            if (field_num == 3) {
                frame->method = (int)val;
            } else if (field_num == 4) {
                frame->seq_id = (int64_t)val;
            } else if (field_num == 5) {
                frame->log_id = (int64_t)val;
            }
        } else {
            offset = prev_offset;
            break;
        }
    }
    
    // Copy headers to frame
    if (header_count > 0) {
        frame->headers = (PbHeader *)malloc(header_count * sizeof(PbHeader));
        if (frame->headers) {
            memcpy(frame->headers, headers, header_count * sizeof(PbHeader));
            frame->header_count = header_count;
        }
    }
    
    return true;
}

// 编码 PING 帧
static size_t pb_encode_ping_frame(uint8_t *buf, size_t buf_size, int64_t service_id) {
    size_t len = 0;
    
    // Header: type=ping
    const char *type_key = "type";
    const char *type_val = MSG_TYPE_PING;
    
    // Encode header message
    uint8_t header_buf[64];
    size_t header_len = 0;
    header_len = pb_write_string(header_buf, 1, type_key, strlen(type_key));
    header_len += pb_write_string(header_buf + header_len, 2, type_val, strlen(type_val));
    
    // Encode frame
    len = pb_write_string(buf, 1, (char *)header_buf, header_len);  // headers
    
    // Service ID as string
    char service_str[32];
    snprintf(service_str, sizeof(service_str), "%ld", (long)service_id);
    len += pb_write_string(buf + len, 2, service_str, strlen(service_str));
    
    // Method = CONTROL
    len += pb_write_varint_field(buf + len, 3, FRAME_TYPE_CONTROL);
    
    // SeqID = 0
    len += pb_write_varint_field(buf + len, 4, 0);
    
    // LogID = 0
    len += pb_write_varint_field(buf + len, 5, 0);
    
    (void)buf_size;
    return len;
}

// 编码响应帧
static size_t pb_encode_response_frame(uint8_t *buf, size_t buf_size, 
                                        PbFrame *req_frame, const char *response_json) {
    size_t len = 0;
    
    // Copy headers from request
    for (int i = 0; i < req_frame->header_count; i++) {
        uint8_t header_buf[256];
        size_t header_len = 0;
        header_len = pb_write_string(header_buf, 1, req_frame->headers[i].key, 
                                     strlen(req_frame->headers[i].key));
        header_len += pb_write_string(header_buf + header_len, 2, 
                                      req_frame->headers[i].value, 
                                      strlen(req_frame->headers[i].value));
        len += pb_write_string(buf + len, 1, (char *)header_buf, header_len);
    }
    
    // Service ID as string
    char service_str[32];
    snprintf(service_str, sizeof(service_str), "%ld", (long)req_frame->service_id);
    len += pb_write_string(buf + len, 2, service_str, strlen(service_str));
    
    // Method = DATA
    len += pb_write_varint_field(buf + len, 3, FRAME_TYPE_DATA);
    
    // SeqID
    len += pb_write_varint_field(buf + len, 4, req_frame->seq_id);
    
    // LogID
    len += pb_write_varint_field(buf + len, 5, req_frame->log_id);
    
    // Payload
    len += pb_write_string(buf + len, 6, response_json, strlen(response_json));
    
    (void)buf_size;
    return len;
}

// ==================== 飞书 WebSocket 实现 ====================

// 获取 WebSocket 连接端点 URL
char* feishu_ws_get_endpoint(const char *app_id, const char *app_secret, const char *domain) {
    if (!app_id || !app_secret) return NULL;
    
    const char *base_domain = domain ? domain : FEISHU_DEFAULT_DOMAIN;
    
    // 构建请求 URL
    char url[512];
    snprintf(url, sizeof(url), "%s%s", base_domain, FEISHU_ENDPOINT_URI);
    
    log_info("[FeishuWS] Getting endpoint from: %s", url);
    
    // 构建请求体
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "AppID", app_id);
    cJSON_AddStringToObject(body, "AppSecret", app_secret);
    char *body_str = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    
    // 发送请求
    HttpRequest req = {
        .url = url,
        .method = "POST",
        .body = body_str,
        .content_type = "application/json",
        .headers = (const char *[]){"locale: zh", NULL},
        .timeout_sec = 10
    };
    
    HttpResponse *resp = http_request(&req);
    free(body_str);
    
    if (!resp) {
        log_error("[FeishuWS] Failed to get endpoint: no response");
        return NULL;
    }
    
    if (!resp->success) {
        log_error("[FeishuWS] Failed to get endpoint: HTTP %d, body: %s", 
                  resp->status_code, resp->body ? resp->body : "(empty)");
        http_response_free(resp);
        return NULL;
    }
    
    // 解析响应
    cJSON *json = cJSON_Parse(resp->body);
    http_response_free(resp);
    
    if (!json) {
        log_error("[FeishuWS] Failed to parse endpoint response");
        return NULL;
    }
    
    // 检查错误码
    cJSON *code = cJSON_GetObjectItem(json, "code");
    if (code && cJSON_IsNumber(code) && code->valueint != 0) {
        cJSON *msg = cJSON_GetObjectItem(json, "msg");
        log_error("[FeishuWS] Get endpoint failed: code=%d, msg=%s", 
                  code->valueint, msg ? msg->valuestring : "unknown");
        cJSON_Delete(json);
        return NULL;
    }
    
    // 获取 URL
    cJSON *data = cJSON_GetObjectItem(json, "data");
    char *result = NULL;
    if (data) {
        cJSON *url_item = cJSON_GetObjectItem(data, "URL");
        if (url_item && cJSON_IsString(url_item)) {
            result = strdup(url_item->valuestring);
            log_info("[FeishuWS] Got endpoint URL: %s", result);
        }
    }
    
    cJSON_Delete(json);
    return result;
}

// 创建飞书 WebSocket 客户端
FeishuWsClient* feishu_ws_create(const char *app_id, const char *app_secret) {
    if (!app_id || !app_secret) return NULL;
    
    FeishuWsClient *client = (FeishuWsClient *)calloc(1, sizeof(FeishuWsClient));
    if (!client) return NULL;
    
    client->config.app_id = strdup(app_id);
    client->config.app_secret = strdup(app_secret);
    client->config.domain = strdup(FEISHU_DEFAULT_DOMAIN);
    client->config.ping_interval_sec = 120;
    client->config.reconnect_interval_sec = 120;
    client->config.max_reconnect_count = -1;  // 无限重连
    
    return client;
}

// 销毁飞书 WebSocket 客户端
void feishu_ws_destroy(FeishuWsClient *client) {
    if (!client) return;
    
    feishu_ws_stop(client);
    
    free(client->config.app_id);
    free(client->config.app_secret);
    free(client->config.domain);
    free(client->config.conn_url);
    free(client->config.conn_id);
    free(client->config.service_id);
    
    if (client->ws_client) {
        ws_client_destroy(client->ws_client);
    }
    
    free(client);
}

// WebSocket 消息回调
static bool feishu_ws_on_message(WsClient *ws, const char *data, size_t len, void *user_data) {
    FeishuWsClient *client = (FeishuWsClient *)user_data;
    
    // 解码 frame
    PbFrame frame;
    if (!pb_decode_frame((const uint8_t *)data, len, &frame)) {
        log_error("[FeishuWS] Failed to decode frame");
        return true;  // Continue receiving
    }
    
    log_debug("[FeishuWS] Received frame: method=%d, header_count=%d, payload_len=%zu",
              frame.method, frame.header_count, frame.payload_len);
    
    if (frame.method == FRAME_TYPE_CONTROL) {
        // Control frame (PING/PONG)
        char *type = pb_frame_get_header(&frame, "type");
        if (type) {
            if (strcmp(type, MSG_TYPE_PING) == 0) {
                log_debug("[FeishuWS] Received PING");
            } else if (strcmp(type, MSG_TYPE_PONG) == 0) {
                log_debug("[FeishuWS] Received PONG");
                // PONG 可能包含配置更新
                if (frame.payload && frame.payload_len > 0) {
                    log_info("[FeishuWS] PONG payload: %.*s", (int)frame.payload_len, frame.payload);
                }
            }
        }
    } else if (frame.method == FRAME_TYPE_DATA) {
        // Data frame (event/card)
        char *type = pb_frame_get_header(&frame, "type");
        char *msg_id = pb_frame_get_header(&frame, "message_id");
        char *trace_id = pb_frame_get_header(&frame, "trace_id");
        
        log_debug("[FeishuWS] Data frame: type=%s, msg_id=%s, trace_id=%s",
                  type ? type : "unknown", 
                  msg_id ? msg_id : "unknown",
                  trace_id ? trace_id : "unknown");
        
        if (type && strcmp(type, MSG_TYPE_EVENT) == 0 && frame.payload) {
            // 处理事件
            log_info("[FeishuWS] Event payload: %.*s", (int)frame.payload_len, frame.payload);
            
            // 解析事件并发送到 agent
            FeishuMessage *msg = feishu_parse_message((const char *)frame.payload);
            if (msg && msg->content) {
                log_info("[FeishuWS] Message from %s: %s", 
                         msg->sender_id ? msg->sender_id : "unknown",
                         msg->content);
                
                // 构建带有上下文的消息
                char *context_msg = (char *)malloc(512 + strlen(msg->content));
                if (context_msg) {
                    snprintf(context_msg, 512 + strlen(msg->content),
                             "[feishu:%s:%s] %s",
                             msg->sender_id ? msg->sender_id : "unknown",
                             msg->chat_id ? msg->chat_id : (msg->message_id ? msg->message_id : "unknown"),
                             msg->content);
                    
                    // 发送到 agent
                    if (agent_send_message(context_msg)) {
                        log_info("[FeishuWS] Message sent to agent successfully");
                    } else {
                        log_error("[FeishuWS] Failed to send message to agent");
                    }
                    free(context_msg);
                }
                
                feishu_message_free(msg);
            }
            
            // 发送响应
            uint8_t resp_buf[4096];
            size_t resp_len = pb_encode_response_frame(resp_buf, sizeof(resp_buf), 
                                                        &frame, "{\"code\":0,\"msg\":\"success\"}");
            ws_client_send_binary(ws, resp_buf, resp_len);
        }
    }
    
    pb_frame_free(&frame);
    return true;  // Continue receiving
}

// WebSocket 状态变化回调
static void feishu_ws_on_state_change(WsClient *ws, WsClientState state, void *user_data) {
    FeishuWsClient *client = (FeishuWsClient *)user_data;
    
    const char *state_str = "unknown";
    switch (state) {
        case WS_CLIENT_DISCONNECTED: state_str = "disconnected"; break;
        case WS_CLIENT_CONNECTING: state_str = "connecting"; break;
        case WS_CLIENT_CONNECTED: state_str = "connected"; break;
        case WS_CLIENT_CLOSING: state_str = "closing"; break;
        case WS_CLIENT_ERROR: state_str = "error"; break;
    }
    
    log_info("[FeishuWS] State changed: %s", state_str);
    (void)client;
    (void)ws;
}

// 启动连接
bool feishu_ws_start(FeishuWsClient *client) {
    if (!client || client->running) return false;
    
    // 获取连接端点
    char *endpoint_url = feishu_ws_get_endpoint(client->config.app_id, 
                                                 client->config.app_secret, 
                                                 client->config.domain);
    if (!endpoint_url) {
        log_error("[FeishuWS] Failed to get endpoint URL");
        return false;
    }
    
    // 解析 URL 获取 service_id
    char *service_id = strstr(endpoint_url, "service_id=");
    if (service_id) {
        service_id += 11;
        char *end = strchr(service_id, '&');
        if (end) *end = '\0';
        free(client->config.service_id);
        client->config.service_id = strdup(service_id);
        if (end) *end = '&';
    }
    
    // 创建 WebSocket 客户端
    WsClientConfig ws_config = {
        .url = endpoint_url,
        .ping_interval_sec = client->config.ping_interval_sec,
        .reconnect_interval_sec = client->config.reconnect_interval_sec,
        .max_reconnect_count = client->config.max_reconnect_count,
        .connect_timeout_sec = 30
    };
    
    client->ws_client = ws_client_create(&ws_config);
    free(endpoint_url);
    
    if (!client->ws_client) {
        log_error("[FeishuWS] Failed to create WebSocket client");
        return false;
    }
    
    // 设置回调
    ws_client_set_message_callback(client->ws_client, feishu_ws_on_message, client);
    ws_client_set_state_callback(client->ws_client, feishu_ws_on_state_change, client);
    
    // 启动
    client->running = ws_client_start(client->ws_client);
    
    if (client->running) {
        log_info("[FeishuWS] Started successfully");
    } else {
        log_error("[FeishuWS] Failed to start");
    }
    
    return client->running;
}

// 停止连接
void feishu_ws_stop(FeishuWsClient *client) {
    if (!client) return;
    
    client->running = false;
    
    if (client->ws_client) {
        ws_client_stop(client->ws_client);
    }
    
    log_info("[FeishuWS] Stopped");
}

// 检查是否已连接
bool feishu_ws_is_connected(FeishuWsClient *client) {
    return client && client->ws_client && ws_client_is_connected(client->ws_client);
}

// 设置飞书域名
void feishu_ws_set_domain(FeishuWsClient *client, const char *domain) {
    if (client && domain) {
        free(client->config.domain);
        client->config.domain = strdup(domain);
    }
}

// 设置心跳间隔
void feishu_ws_set_ping_interval(FeishuWsClient *client, int seconds) {
    if (client && seconds > 0) {
        client->config.ping_interval_sec = seconds;
    }
}

// 设置重连配置
void feishu_ws_set_reconnect(FeishuWsClient *client, int interval_sec, int max_count) {
    if (client) {
        if (interval_sec > 0) client->config.reconnect_interval_sec = interval_sec;
        client->config.max_reconnect_count = max_count;
    }
}

// 设置用户数据
void feishu_ws_set_user_data(FeishuWsClient *client, void *user_data) {
    if (client) {
        client->user_data = user_data;
    }
}
