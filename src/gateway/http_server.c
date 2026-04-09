// Platform-specific includes
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#define msleep(ms) Sleep(ms)
#define SOCKET int
#define INVALID_SOCKET (-1)
#define CLOSESOCKET(s) closesocket(s)
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
#define msleep(ms) do { struct timespec ts = {0, (ms)*1000000L}; nanosleep(&ts, NULL); } while(0)
#define CLOSESOCKET(s) close(s)
#endif

#include "http_server.h"
#include "../common/log.h"
#include "../common/cJSON.h"
#include "../common/utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include <errno.h>

#define BUFFER_SIZE 8192
#define MAX_HEADERS 4096

// HTTP 状态码描述
static const char* http_status_text(int status_code) {
    switch (status_code) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 500: return "Internal Server Error";
        case 503: return "Service Unavailable";
        default: return "Unknown";
    }
}

// URL 解码
char* srv_url_decode(const char* src) {
    if (!src) return NULL;
    
    size_t len = strlen(src);
    char* dest = (char*)malloc(len + 1);
    if (!dest) return NULL;
    
    char* p = dest;
    while (*src) {
        if (*src == '%' && src[1] && src[2]) {
            char hex[3] = {src[1], src[2], 0};
            *p++ = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            *p++ = ' ';
            src++;
        } else {
            *p++ = *src++;
        }
    }
    *p = '\0';
    
    return dest;
}

// 解析查询参数
char* srv_get_query_param(const char* query, const char* key) {
    if (!query || !key) return NULL;
    
    size_t key_len = strlen(key);
    const char* ptr = query;
    
    while (ptr && *ptr) {
        // 查找 key
        if (strncmp(ptr, key, key_len) == 0 && ptr[key_len] == '=') {
            const char* val_start = ptr + key_len + 1;
            const char* val_end = strchr(val_start, '&');
            
            size_t val_len = val_end ? (size_t)(val_end - val_start) : strlen(val_start);
            char* value = (char*)malloc(val_len + 1);
            if (!value) return NULL;
            
            strncpy(value, val_start, val_len);
            value[val_len] = '\0';
            
            // URL 解码
            char* decoded = srv_url_decode(value);
            free(value);
            return decoded;
        }
        
        // 移动到下一个参数
        ptr = strchr(ptr, '&');
        if (ptr) ptr++;
    }
    
    return NULL;
}

// 创建 HTTP 响应
SrvResponse* srv_response_create(int status_code, const char* content_type, const char* body) {
    SrvResponse* response = (SrvResponse*)calloc(1, sizeof(SrvResponse));
    if (!response) return NULL;
    
    response->status_code = status_code;
    response->stream = false;
    
    if (content_type) {
        strncpy(response->content_type, content_type, sizeof(response->content_type) - 1);
    } else {
        strcpy(response->content_type, "text/plain");
    }
    
    if (body) {
        response->body_len = strlen(body);
        response->body = strdup(body);
    }
    
    return response;
}

// 创建 JSON 响应
SrvResponse* srv_response_json(int status_code, const char* json) {
    return srv_response_create(status_code, "application/json", json);
}

// 创建错误响应
SrvResponse* srv_response_error(int status_code, const char* message) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "error", message);
    cJSON_AddNumberToObject(root, "status", status_code);
    
    char* json_str = cJSON_Print(root);
    cJSON_Delete(root);
    
    SrvResponse* response = srv_response_json(status_code, json_str);
    free(json_str);
    
    return response;
}

// 创建流式响应
SrvResponse* srv_response_stream(const char* content_type) {
    SrvResponse* response = (SrvResponse*)calloc(1, sizeof(SrvResponse));
    if (!response) return NULL;
    
    response->status_code = 200;
    response->stream = true;
    
    if (content_type) {
        strncpy(response->content_type, content_type, sizeof(response->content_type) - 1);
    } else {
        strcpy(response->content_type, "text/event-stream");
    }
    
    return response;
}

// 释放响应
void srv_response_free(SrvResponse* response) {
    if (!response) return;
    if (response->body) free(response->body);
    free(response);
}

// 释放请求
void srv_request_free(SrvRequest* request) {
    if (!request) return;
    if (request->body) free(request->body);
    if (request->headers) free(request->headers);
    free(request);
}

// 发送 SSE 事件
bool srv_send_sse_event(int client_socket, const char* event, const char* data) {
    char buffer[BUFFER_SIZE];
    int len = 0;
    
    if (event && strlen(event) > 0) {
        len += snprintf(buffer + len, sizeof(buffer) - len, "event: %s\n", event);
    }
    
    if (data) {
        // 多行数据需要用多个 data: 前缀
        const char* ptr = data;
        while (*ptr) {
            const char* nl = strchr(ptr, '\n');
            if (nl) {
                len += snprintf(buffer + len, sizeof(buffer) - len, "data: %.*s\n", (int)(nl - ptr), ptr);
                ptr = nl + 1;
            } else {
                len += snprintf(buffer + len, sizeof(buffer) - len, "data: %s\n", ptr);
                break;
            }
        }
    }
    
    len += snprintf(buffer + len, sizeof(buffer) - len, "\n");
    
    return send(client_socket, buffer, len, 0) == len;
}

// 发送 SSE 完成
bool srv_send_sse_done(int client_socket) {
    return srv_send_sse_event(client_socket, "done", "[DONE]");
}

// 解析 HTTP 请求
static SrvRequest* parse_request(const char* buffer, size_t len) {
    SrvRequest* request = (SrvRequest*)calloc(1, sizeof(SrvRequest));
    if (!request) return NULL;
    
    // 解析请求行
    if (sscanf(buffer, "%15s %255s", request->method, request->path) != 2) {
        srv_request_free(request);
        return NULL;
    }
    
    // 查找 headers 和 body 分隔
    const char* header_end = strstr(buffer, "\r\n\r\n");
    if (!header_end) {
        header_end = strstr(buffer, "\n\n");
        if (!header_end) {
            srv_request_free(request);
            return NULL;
        }
        header_end += 2;
    } else {
        header_end += 4;
    }
    
    // 复制 headers
    size_t header_len = header_end - buffer;
    request->headers = (char*)malloc(header_len + 1);
    if (request->headers) {
        memcpy(request->headers, buffer, header_len);
        request->headers[header_len] = '\0';
    }
    
    // 解析 Content-Type
    const char* ct = http_strcasestr(buffer, "Content-Type:");
    if (ct) {
        ct += 13;
        while (*ct == ' ') ct++;
        const char* ct_end = strstr(ct, "\r\n");
        if (!ct_end) ct_end = strstr(ct, "\n");
        if (ct_end) {
            size_t ct_len = ct_end - ct;
            if (ct_len >= sizeof(request->content_type)) ct_len = sizeof(request->content_type) - 1;
            strncpy(request->content_type, ct, ct_len);
            request->content_type[ct_len] = '\0';
        }
    }
    
    // 复制 body
    size_t body_offset = header_end - buffer;
    if (len > body_offset) {
        request->body_len = len - body_offset;
        request->body = (char*)malloc(request->body_len + 1);
        if (request->body) {
            memcpy(request->body, header_end, request->body_len);
            request->body[request->body_len] = '\0';
        }
    }
    
    return request;
}

// 发送 HTTP 响应头
static void send_response_headers(int client_socket, SrvResponse* response, bool stream) {
    char headers[BUFFER_SIZE];
    int len = 0;
    
    len += snprintf(headers + len, sizeof(headers) - len,
                    "HTTP/1.1 %d %s\r\n"
                    "Content-Type: %s\r\n"
                    "Connection: %s\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                    "Access-Control-Allow-Headers: Content-Type\r\n",
                    response->status_code, http_status_text(response->status_code),
                    response->content_type,
                    stream ? "keep-alive" : "close");
    
    if (stream) {
        len += snprintf(headers + len, sizeof(headers) - len,
                       "Cache-Control: no-cache\r\n"
                       "X-Accel-Buffering: no\r\n");
    } else if (response->body_len > 0) {
        len += snprintf(headers + len, sizeof(headers) - len,
                       "Content-Length: %zu\r\n", response->body_len);
    }
    
    len += snprintf(headers + len, sizeof(headers) - len, "\r\n");
    send(client_socket, headers, len, 0);
}

// 处理单个连接
static void handle_connection(int client_socket, HttpServer* server) {
    char buffer[BUFFER_SIZE];
    int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    
    if (bytes_received <= 0) {
        CLOSESOCKET(client_socket);
        return;
    }
    
    buffer[bytes_received] = '\0';
    
    // 检查是否是 OPTIONS 请求 (CORS preflight)
    if (strncmp(buffer, "OPTIONS", 7) == 0) {
        char response[] = "HTTP/1.1 204 No Content\r\n"
                         "Access-Control-Allow-Origin: *\r\n"
                         "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                         "Access-Control-Allow-Headers: Content-Type\r\n"
                         "Connection: close\r\n\r\n";
        send(client_socket, response, strlen(response), 0);
        CLOSESOCKET(client_socket);
        return;
    }
    
    // 解析请求
    SrvRequest* request = parse_request(buffer, bytes_received);
    if (!request) {
        SrvResponse* error = srv_response_error(400, "Invalid request");
        send_response_headers(client_socket, error, false);
        srv_response_free(error);
        CLOSESOCKET(client_socket);
        return;
    }
    
    // 检查授权
    if (server->api_key && server->api_key[0] != '\0') {
        bool authorized = false;
        
        // 检查 Authorization header: "Bearer <token>"
        if (request->headers) {
            const char* auth = http_strcasestr(request->headers, "Authorization:");
            if (auth) {
                auth += 14; // skip "Authorization:"
                while (*auth == ' ') auth++;
                
                // 支持 "Bearer <token>" 格式
                if (strncasecmp(auth, "Bearer ", 7) == 0) {
                    auth += 7;
                    const char* auth_end = strstr(auth, "\r\n");
                    if (!auth_end) auth_end = strstr(auth, "\n");
                    
                    size_t token_len = auth_end ? (size_t)(auth_end - auth) : strlen(auth);
                    if (token_len == strlen(server->api_key) && 
                        strncmp(auth, server->api_key, token_len) == 0) {
                        authorized = true;
                    }
                }
                
                // 也支持直接传 token
                if (!authorized) {
                    const char* auth_end = strstr(auth, "\r\n");
                    if (!auth_end) auth_end = strstr(auth, "\n");
                    size_t token_len = auth_end ? (size_t)(auth_end - auth) : strlen(auth);
                    if (token_len == strlen(server->api_key) && 
                        strncmp(auth, server->api_key, token_len) == 0) {
                        authorized = true;
                    }
                }
            }
            
            // 检查 X-API-Key header
            if (!authorized) {
                const char* api_key_header = http_strcasestr(request->headers, "X-API-Key:");
                if (api_key_header) {
                    api_key_header += 10;
                    while (*api_key_header == ' ') api_key_header++;
                    const char* key_end = strstr(api_key_header, "\r\n");
                    if (!key_end) key_end = strstr(api_key_header, "\n");
                    size_t key_len = key_end ? (size_t)(key_end - api_key_header) : strlen(api_key_header);
                    if (key_len == strlen(server->api_key) && 
                        strncmp(api_key_header, server->api_key, key_len) == 0) {
                        authorized = true;
                    }
                }
            }
        }
        
        if (!authorized) {
            log_warn("HTTP request unauthorized: %s %s", request->method, request->path);
            char response[] = "HTTP/1.1 401 Unauthorized\r\n"
                             "Content-Type: application/json\r\n"
                             "Access-Control-Allow-Origin: *\r\n"
                             "WWW-Authenticate: Bearer\r\n"
                             "Connection: close\r\n\r\n"
                             "{\"error\":\"Unauthorized\",\"status\":401}";
            send(client_socket, response, strlen(response), 0);
            srv_request_free(request);
            CLOSESOCKET(client_socket);
            return;
        }
    }
    
    log_debug("HTTP Request: %s %s", request->method, request->path);
    
    // 检查是否需要流式响应
    bool is_stream_request = false;
    
    // 检查 Accept header
    if (http_strcasestr(request->headers ? request->headers : "", "text/event-stream")) {
        is_stream_request = true;
    }
    
    // 检查 query parameter
    if (strstr(request->path, "stream=true") || strstr(request->path, "stream=1")) {
        is_stream_request = true;
    }
    
    // 检查 body 中的 stream 参数
    if (request->body && http_strcasestr(request->body, "\"stream\":true")) {
        is_stream_request = true;
    }
    
    SrvResponse* response = NULL;
    
    if (is_stream_request && server->stream_handler) {
        // 流式响应
        response = srv_response_stream("text/event-stream");
        send_response_headers(client_socket, response, true);
        
        // 定义流式回调
        typedef struct {
            int socket;
        } StreamContext;
        
        StreamContext ctx = { .socket = client_socket };
        
        // 调用流式处理器
        bool success = server->stream_handler(request, 
            (SrvStreamCallback)srv_send_sse_event, 
            &ctx);
        
        if (success) {
            srv_send_sse_done(client_socket);
        }
        
        srv_response_free(response);
    } else if (server->handler) {
        // 普通响应
        response = server->handler(request, server->user_data);
        
        if (response) {
            send_response_headers(client_socket, response, false);
            if (response->body && response->body_len > 0) {
                send(client_socket, response->body, response->body_len, 0);
            }
            srv_response_free(response);
        } else {
            SrvResponse* error = srv_response_error(500, "Handler error");
            send_response_headers(client_socket, error, false);
            srv_response_free(error);
        }
    } else {
        // 没有处理器
        response = srv_response_error(404, "Not found");
        send_response_headers(client_socket, response, false);
        srv_response_free(response);
    }
    
    srv_request_free(request);
    CLOSESOCKET(client_socket);
}

// 服务器线程
static void* server_thread(void* arg) {
    HttpServer* server = (HttpServer*)arg;

    log_info("HTTP server started on port %d", server->port);

    while (server->running) {
        fd_set read_fds;
        struct timeval timeout;

        // Check if socket is valid before using it
        if (server->socket < 0) {
            // Socket is invalid, sleep and continue
            msleep(100);
            continue;
        }

        // Check if socket exceeds FD_SETSIZE (typically 1024)
        if (server->socket >= FD_SETSIZE) {
            log_error("HTTP server socket %d exceeds FD_SETSIZE %d", server->socket, FD_SETSIZE);
            msleep(100);
            continue;
        }

        FD_ZERO(&read_fds);
        FD_SET(server->socket, &read_fds);

        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int activity = select(server->socket + 1, &read_fds, NULL, NULL, &timeout);

        if (activity < 0) {
            if (errno != EINTR) {
                log_error("HTTP server select error: %d (socket=%d, FD_SETSIZE=%d)", 
                         errno, server->socket, FD_SETSIZE);
                // Sleep to avoid busy loop on error
                msleep(100);
            }
            continue;
        }

        if (activity == 0) {
            // Timeout, just continue
            continue;
        }

        if (!FD_ISSET(server->socket, &read_fds)) {
            continue;
        }

        // 接受新连接
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_socket = accept(server->socket, (struct sockaddr*)&client_addr, &client_len);

        if (client_socket < 0) {
            log_error("HTTP server accept error: %d", errno);
            continue;
        }

        log_debug("HTTP client connected");

        // 处理连接 (简单实现：在主线程处理)
        handle_connection(client_socket, server);
    }

    log_info("HTTP server stopped");
    return NULL;
}

// 创建 HTTP 服务器
HttpServer* http_server_create(const SrvConfig* config) {
    HttpServer* server = (HttpServer*)calloc(1, sizeof(HttpServer));
    if (!server) return NULL;
    
    server->port = config->port ? config->port : 8080;
    server->max_connections = config->max_connections ? config->max_connections : 100;
    server->handler = config->handler;
    server->stream_handler = config->stream_handler;
    server->user_data = config->user_data;
    server->running = false;
    
    // 复制 API key
    if (config->api_key) {
        server->api_key = strdup(config->api_key);
    }
    
    // 创建 socket
    server->socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server->socket < 0) {
        log_error("Failed to create HTTP socket");
        free(server->api_key);
        free(server);
        return NULL;
    }
    
    // 设置 socket 选项
    int opt = 1;
#ifdef _WIN32
    setsockopt(server->socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
    setsockopt(server->socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif
    
    // 绑定端口
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(server->port);

    if (bind(server->socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        log_error("Failed to bind HTTP socket to port %d: %s", server->port, strerror(errno));
        CLOSESOCKET(server->socket);
        free(server->api_key);
        free(server);
        return NULL;
    }

    // 监听
    if (listen(server->socket, 10) < 0) {
        log_error("Failed to listen on HTTP socket: %s", strerror(errno));
        CLOSESOCKET(server->socket);
        free(server->api_key);
        free(server);
        return NULL;
    }

    log_info("HTTP server created on port %d (socket=%d)", server->port, server->socket);
    return server;
}

// 启动服务器
bool http_server_start(HttpServer* server) {
    if (!server || server->running) return false;
    
    server->running = true;
    
    if (pthread_create(&server->thread, NULL, server_thread, server) != 0) {
        log_error("Failed to create HTTP server thread");
        server->running = false;
        return false;
    }
    
    return true;
}

// 停止服务器
void http_server_stop(HttpServer* server) {
    if (!server) return;
    
    server->running = false;
    
    // 等待线程结束
    pthread_join(server->thread, NULL);
    
    if (server->socket >= 0) {
        CLOSESOCKET(server->socket);
        server->socket = -1;
    }
    
    log_info("HTTP server stopped");
}

// 销毁服务器
void http_server_destroy(HttpServer* server) {
    if (!server) return;
    
    if (server->running) {
        http_server_stop(server);
    }
    
    if (server->socket >= 0) {
        CLOSESOCKET(server->socket);
    }
    
    free(server->api_key);
    free(server);
}
