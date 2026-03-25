/**
 * HTTP Client - 用于调用外部 HTTP API
 * 支持 GET/POST、流式响应、自定义请求头
 */
#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stdbool.h>
#include <stddef.h>

// HTTP 响应结构
typedef struct {
    int status_code;       // HTTP 状态码
    char* headers;         // 响应头
    char* body;            // 响应体
    size_t body_len;       // 响应体长度
    char* content_type;    // Content-Type
    bool success;          // 请求是否成功
} HttpResponse;

// 流式回调函数类型
// data: 接收到的数据块, len: 数据长度, user_data: 用户数据
// 返回 true 继续接收，false 中断
typedef bool (*HttpStreamCallback)(const char* data, size_t len, void* user_data);

// HTTP 请求配置
typedef struct {
    const char* url;           // 请求 URL
    const char* method;        // 请求方法 (GET/POST/PUT/DELETE)
    const char* body;          // 请求体 (POST/PUT)
    const char* content_type;  // Content-Type
    const char** headers;      // 自定义请求头数组 {"Header: Value", NULL}
    int timeout_sec;           // 超时时间(秒), 0 表示默认
    bool follow_redirects;     // 是否跟随重定向
} HttpRequest;

// HTTP Headers 结构 (用于动态构建请求头)
typedef struct HttpHeaders HttpHeaders;

// 创建新的 headers
HttpHeaders* http_headers_new(void);

// 添加 header
void http_headers_add(HttpHeaders* headers, const char* key, const char* value);

// 释放 headers
void http_headers_free(HttpHeaders* headers);

// 将 headers 转换为字符串数组 (内部使用)
const char** http_headers_to_array(HttpHeaders* headers);

// ==================== 基础接口 ====================

// 初始化 HTTP 客户端 (程序启动时调用一次)
bool http_client_init(void);

// 清理 HTTP 客户端 (程序退出时调用)
void http_client_cleanup(void);

// ==================== 简单接口 ====================

// HTTP GET 请求
HttpResponse* http_get(const char* url);

// HTTP GET 请求 (带自定义 headers)
HttpResponse* http_get_with_headers(const char* url, HttpHeaders* headers);

// HTTP POST 请求 (JSON)
HttpResponse* http_post(const char* url, const char* json_body);

// HTTP POST 请求 (JSON，带自定义 headers)
HttpResponse* http_post_json_with_headers(const char* url, const char* json_body, HttpHeaders* headers);

// HTTP POST 请求 (自定义 Content-Type)
HttpResponse* http_post_data(const char* url, const char* body, 
                              const char* content_type);

// ==================== 高级接口 ====================

// 发送请求 (自定义配置)
HttpResponse* http_request(const HttpRequest* req);

// 流式请求 (SSE/流式 API)
// 回调函数会在接收到数据时被调用
bool http_request_stream(const HttpRequest* req, 
                          HttpStreamCallback callback, 
                          void* user_data);

// ==================== 响应处理 ====================

// 释放响应
void http_response_free(HttpResponse* resp);

// 从响应中解析 JSON 字段 (需要调用方 free)
char* http_response_json_string(HttpResponse* resp, const char* key);
int http_response_json_int(HttpResponse* resp, const char* key, int default_val);

// ==================== 工具函数 ====================

// URL 编码 (返回值需要 free)
char* http_url_encode(const char* str);

// URL 解码 (返回值需要 free)
char* http_url_decode(const char* str);

// 构建 URL 查询字符串 (返回值需要 free)
// params: {"key1", "value1", "key2", "value2", NULL}
char* http_build_query(const char* base_url, const char** params);

#endif // HTTP_CLIENT_H
