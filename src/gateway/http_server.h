#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <stdbool.h>
#include <stddef.h>
#include <pthread.h>

// HTTP 服务器请求结构
typedef struct {
    char method[16];        // GET, POST, etc.
    char path[256];         // Request path
    char* body;             // Request body (for POST)
    size_t body_len;        // Body length
    char content_type[64];  // Content-Type header
    char* headers;          // Raw headers
} SrvRequest;

// HTTP 服务器响应结构
typedef struct {
    int status_code;        // HTTP status code
    char* body;             // Response body
    size_t body_len;        // Body length
    char content_type[64];  // Content-Type
    bool stream;            // Is streaming response
} SrvResponse;

// 流式回调函数类型
typedef void (*SrvStreamCallback)(const char* chunk, size_t len, void* user_data);

// 请求处理函数类型
typedef SrvResponse* (*SrvRequestHandler)(const SrvRequest* request, void* user_data);

// 流式请求处理函数类型
typedef bool (*SrvStreamHandler)(const SrvRequest* request, 
                                  SrvStreamCallback callback, 
                                  void* user_data);

// HTTP 服务器配置
typedef struct {
    int port;                       // 监听端口
    int max_connections;            // 最大连接数
    SrvRequestHandler handler;      // 普通请求处理器
    SrvStreamHandler stream_handler; // 流式请求处理器
    void* user_data;                // 用户数据
    const char* api_key;            // API 授权密钥 (可选)
} SrvConfig;

// HTTP 服务器结构
typedef struct {
    int socket;
    int port;
    bool running;
    int max_connections;
    SrvRequestHandler handler;
    SrvStreamHandler stream_handler;
    void* user_data;
    pthread_t thread;
    char* api_key;                  // API 授权密钥
} HttpServer;

// ==================== 服务器 API ====================

// 创建 HTTP 服务器
HttpServer* http_server_create(const SrvConfig* config);

// 启动服务器
bool http_server_start(HttpServer* server);

// 停止服务器
void http_server_stop(HttpServer* server);

// 销毁服务器
void http_server_destroy(HttpServer* server);

// ==================== 请求/响应 API ====================

// 创建响应
SrvResponse* srv_response_create(int status_code, const char* content_type, const char* body);

// 创建 JSON 响应
SrvResponse* srv_response_json(int status_code, const char* json);

// 创建错误响应
SrvResponse* srv_response_error(int status_code, const char* message);

// 创建流式响应
SrvResponse* srv_response_stream(const char* content_type);

// 释放响应
void srv_response_free(SrvResponse* response);

// 释放请求
void srv_request_free(SrvRequest* request);

// ==================== 工具函数 ====================

// URL 解码 (服务器内部使用)
char* srv_url_decode(const char* src);

// 解析查询参数
char* srv_get_query_param(const char* query, const char* key);

// 发送 SSE 事件
bool srv_send_sse_event(int client_socket, const char* event, const char* data);

// 发送 SSE 完成
bool srv_send_sse_done(int client_socket);

#endif // HTTP_SERVER_H
