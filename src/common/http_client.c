/**
 * HTTP Client 实现 - 基于 libcurl
 */
#include "http_client.h"
#include "log.h"
#include "cJSON.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef HAVE_CURL
#include <curl/curl.h>

// 全局初始化标志
static bool g_curl_initialized = false;

// 响应缓冲区
typedef struct {
    char* data;
    size_t size;
    size_t capacity;
} ResponseBuffer;

// 流式回调上下文
typedef struct {
    HttpStreamCallback callback;
    void* user_data;
} StreamContext;

// ==================== 内部辅助函数 ====================

static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total_size = size * nmemb;
    ResponseBuffer* buf = (ResponseBuffer*)userp;
    
    // 扩展缓冲区
    size_t new_size = buf->size + total_size + 1;
    if (new_size > buf->capacity) {
        size_t new_capacity = buf->capacity * 2;
        if (new_capacity < new_size) new_capacity = new_size;
        char* new_data = (char*)realloc(buf->data, new_capacity);
        if (!new_data) return 0;  // 内存分配失败
        buf->data = new_data;
        buf->capacity = new_capacity;
    }
    
    memcpy(buf->data + buf->size, contents, total_size);
    buf->size += total_size;
    buf->data[buf->size] = '\0';
    
    return total_size;
}

static size_t stream_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total_size = size * nmemb;
    StreamContext* ctx = (StreamContext*)userp;
    
    if (ctx->callback) {
        if (!ctx->callback((const char*)contents, total_size, ctx->user_data)) {
            return 0;  // 用户中断
        }
    }
    return total_size;
}

static size_t header_callback(char* buffer, size_t size, size_t nitems, void* userdata) {
    size_t total_size = size * nitems;
    ResponseBuffer* buf = (ResponseBuffer*)userdata;
    
    // 扩展缓冲区
    size_t new_size = buf->size + total_size + 1;
    if (new_size > buf->capacity) {
        size_t new_capacity = buf->capacity * 2;
        if (new_capacity < new_size) new_capacity = new_size;
        char* new_data = (char*)realloc(buf->data, new_capacity);
        if (!new_data) return total_size;
        buf->data = new_data;
        buf->capacity = new_capacity;
    }
    
    memcpy(buf->data + buf->size, buffer, total_size);
    buf->size += total_size;
    buf->data[buf->size] = '\0';
    
    return total_size;
}

// ==================== 初始化/清理 ====================

bool http_client_init(void) {
    if (g_curl_initialized) return true;
    
    CURLcode res = curl_global_init(CURL_GLOBAL_ALL);
    if (res != CURLE_OK) {
        log_error("curl_global_init failed: %s", curl_easy_strerror(res));
        return false;
    }
    
    g_curl_initialized = true;
    log_debug("HTTP client initialized");
    return true;
}

void http_client_cleanup(void) {
    if (g_curl_initialized) {
        curl_global_cleanup();
        g_curl_initialized = false;
        log_debug("HTTP client cleaned up");
    }
}

// ==================== HttpHeaders 实现 ====================

#define MAX_HEADERS 32
#define MAX_HEADER_LEN 1024

typedef struct HttpHeaders {
    char headers[MAX_HEADERS][MAX_HEADER_LEN];
    int count;
} HttpHeaders;

HttpHeaders* http_headers_new(void) {
    HttpHeaders* h = (HttpHeaders*)calloc(1, sizeof(HttpHeaders));
    if (h) {
        h->count = 0;
    }
    return h;
}

void http_headers_add(HttpHeaders* headers, const char* key, const char* value) {
    if (!headers || !key || !value) return;
    if (headers->count >= MAX_HEADERS) return;
    
    snprintf(headers->headers[headers->count], MAX_HEADER_LEN, "%s: %s", key, value);
    headers->count++;
}

void http_headers_free(HttpHeaders* headers) {
    free(headers);
}

// 内部使用：转换为curl可用的字符串数组
static const char** http_headers_to_curl_array(HttpHeaders* headers) {
    if (!headers || headers->count == 0) return NULL;
    
    // 需要NULL结尾的数组
    const char** arr = (const char**)calloc(headers->count + 1, sizeof(char*));
    if (!arr) return NULL;
    
    for (int i = 0; i < headers->count; i++) {
        arr[i] = headers->headers[i];
    }
    arr[headers->count] = NULL;
    return arr;
}

const char** http_headers_to_array(HttpHeaders* headers) {
    return http_headers_to_curl_array(headers);
}

// ==================== 内部请求实现 ====================

static HttpResponse* do_request(const HttpRequest* req, bool stream, 
                                 HttpStreamCallback callback, void* user_data) {
    if (!g_curl_initialized) {
        if (!http_client_init()) {
            return NULL;
        }
    }
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        log_error("curl_easy_init failed");
        return NULL;
    }
    
    // 准备响应结构
    HttpResponse* resp = (HttpResponse*)calloc(1, sizeof(HttpResponse));
    if (!resp) {
        curl_easy_cleanup(curl);
        return NULL;
    }
    
    ResponseBuffer body_buf = {0};
    ResponseBuffer header_buf = {0};
    body_buf.capacity = 4096;
    body_buf.data = (char*)malloc(body_buf.capacity);
    header_buf.capacity = 1024;
    header_buf.data = (char*)malloc(header_buf.capacity);
    
    if (!body_buf.data || !header_buf.data) {
        free(body_buf.data);
        free(header_buf.data);
        free(resp);
        curl_easy_cleanup(curl);
        return NULL;
    }
    body_buf.data[0] = '\0';
    header_buf.data[0] = '\0';
    
    // 设置 URL
    curl_easy_setopt(curl, CURLOPT_URL, req->url);
    
    // 设置方法
    if (req->method) {
        if (strcmp(req->method, "POST") == 0) {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
        } else if (strcmp(req->method, "PUT") == 0) {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        } else if (strcmp(req->method, "DELETE") == 0) {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
        } else if (strcmp(req->method, "PATCH") == 0) {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
        }
    }
    
    // 设置请求体
    if (req->body) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req->body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, strlen(req->body));
        // log_debug("[HTTP] Request body: %s", req->body);
    }
    
    // 设置请求头
    struct curl_slist* headers = NULL;
    if (req->content_type) {
        char ct[256];
        snprintf(ct, sizeof(ct), "Content-Type: %s", req->content_type);
        headers = curl_slist_append(headers, ct);
    }
    if (req->headers) {
        for (int i = 0; req->headers[i] != NULL; i++) {
            headers = curl_slist_append(headers, req->headers[i]);
        }
    }
    if (headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }
    
    // 设置超时
    if (req->timeout_sec > 0) {
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, req->timeout_sec);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);  // 连接超时 10 秒
    }
    
    // 设置重定向
    if (req->follow_redirects) {
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    }
    
    // 设置回调
    if (stream && callback) {
        StreamContext ctx = {callback, user_data};
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stream_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    } else {
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body_buf);
    }
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &header_buf);
    
    // 执行请求
    CURLcode res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        log_error("curl_easy_perform failed: %s", curl_easy_strerror(res));
        resp->success = false;
        resp->status_code = 0;
        // 释放缓冲区
        free(body_buf.data);
        free(header_buf.data);
    } else {
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        resp->status_code = (int)status;
        resp->success = (status >= 200 && status < 300);
        resp->body = body_buf.data;
        resp->body_len = body_buf.size;
        resp->headers = header_buf.data;
        
        // 调试日志：打印响应内容
        // log_debug("[HTTP] Response: status=%ld, body_len=%zu", status, body_buf.size);
        // if (body_buf.data && body_buf.size > 0) {
        //     // 限制打印长度，避免日志过长
        //     size_t print_len = body_buf.size > 500 ? 500 : body_buf.size;
        //     log_debug("[HTTP] Response body (first %zu bytes): %.*s", print_len, (int)print_len, body_buf.data);
        // }
        
        // 提取 Content-Type
        char* ct_start = http_strcasestr(header_buf.data, "Content-Type:");
        if (ct_start) {
            ct_start += 13;
            while (*ct_start == ' ') ct_start++;
            char* ct_end = strstr(ct_start, "\r\n");
            if (!ct_end) ct_end = strstr(ct_start, "\n");
            if (ct_end) {
                size_t ct_len = ct_end - ct_start;
                resp->content_type = (char*)malloc(ct_len + 1);
                if (resp->content_type) {
                    memcpy(resp->content_type, ct_start, ct_len);
                    resp->content_type[ct_len] = '\0';
                }
            }
        }
        // 注意：不要释放 header_buf.data，因为 resp->headers 指向它
    }
    
    // 清理
    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    return resp;
}

// ==================== 简单接口 ====================

// 默认超时时间（秒）
#define DEFAULT_TIMEOUT_SEC 30

HttpResponse* http_get(const char* url) {
    HttpRequest req = {
        .url = url,
        .method = "GET",
        .follow_redirects = true,
        .timeout_sec = DEFAULT_TIMEOUT_SEC
    };
    return do_request(&req, false, NULL, NULL);
}

HttpResponse* http_get_with_headers(const char* url, HttpHeaders* headers) {
    const char** headers_arr = http_headers_to_curl_array(headers);
    HttpRequest req = {
        .url = url,
        .method = "GET",
        .headers = headers_arr,
        .follow_redirects = true,
        .timeout_sec = DEFAULT_TIMEOUT_SEC
    };
    HttpResponse* resp = do_request(&req, false, NULL, NULL);
    free(headers_arr);
    return resp;
}

HttpResponse* http_post(const char* url, const char* json_body) {
    HttpRequest req = {
        .url = url,
        .method = "POST",
        .body = json_body,
        .content_type = "application/json",
        .follow_redirects = true,
        .timeout_sec = DEFAULT_TIMEOUT_SEC
    };
    return do_request(&req, false, NULL, NULL);
}

HttpResponse* http_post_json_with_headers(const char* url, const char* json_body, HttpHeaders* headers) {
    const char** headers_arr = http_headers_to_curl_array(headers);
    HttpRequest req = {
        .url = url,
        .method = "POST",
        .body = json_body,
        .content_type = "application/json",
        .headers = headers_arr,
        .follow_redirects = true,
        .timeout_sec = DEFAULT_TIMEOUT_SEC
    };
    HttpResponse* resp = do_request(&req, false, NULL, NULL);
    free(headers_arr);
    return resp;
}

HttpResponse* http_post_data(const char* url, const char* body, const char* content_type) {
    HttpRequest req = {
        .url = url,
        .method = "POST",
        .body = body,
        .content_type = content_type,
        .follow_redirects = true,
        .timeout_sec = DEFAULT_TIMEOUT_SEC
    };
    return do_request(&req, false, NULL, NULL);
}

// ==================== 高级接口 ====================

HttpResponse* http_request(const HttpRequest* req) {
    return do_request(req, false, NULL, NULL);
}

HttpResponse* http_request_stream(const HttpRequest* req, 
                          HttpStreamCallback callback, 
                          void* user_data) {
    return do_request(req, true, callback, user_data);
}

// ==================== 响应处理 ====================

void http_response_free(HttpResponse* resp) {
    if (resp) {
        free(resp->body);
        free(resp->headers);
        free(resp->content_type);
        free(resp);
    }
}

char* http_response_json_string(HttpResponse* resp, const char* key) {
    if (!resp || !resp->body || !key) return NULL;
    
    cJSON* root = cJSON_Parse(resp->body);
    if (!root) return NULL;
    
    cJSON* item = cJSON_GetObjectItem(root, key);
    char* result = NULL;
    if (item && cJSON_IsString(item)) {
        result = strdup(item->valuestring);
    }
    
    cJSON_Delete(root);
    return result;
}

int http_response_json_int(HttpResponse* resp, const char* key, int default_val) {
    if (!resp || !resp->body || !key) return default_val;
    
    cJSON* root = cJSON_Parse(resp->body);
    if (!root) return default_val;
    
    cJSON* item = cJSON_GetObjectItem(root, key);
    int result = default_val;
    if (item && cJSON_IsNumber(item)) {
        result = item->valueint;
    }
    
    cJSON_Delete(root);
    return result;
}

// ==================== 工具函数 ====================

char* http_url_encode(const char* str) {
    if (!str) return NULL;
    
    char* encoded = (char*)malloc(strlen(str) * 3 + 1);
    if (!encoded) return NULL;
    
    char* p = encoded;
    while (*str) {
        if (isalnum((unsigned char)*str) || *str == '-' || *str == '_' || 
            *str == '.' || *str == '~') {
            *p++ = *str;
        } else {
            p += sprintf(p, "%%%02X", (unsigned char)*str);
        }
        str++;
    }
    *p = '\0';
    
    return encoded;
}

char* http_url_decode(const char* str) {
    if (!str) return NULL;
    
    char* decoded = (char*)malloc(strlen(str) + 1);
    if (!decoded) return NULL;
    
    char* p = decoded;
    while (*str) {
        if (*str == '%' && isxdigit((unsigned char)str[1]) && isxdigit((unsigned char)str[2])) {
            char hex[3] = {str[1], str[2], 0};
            *p++ = (char)strtol(hex, NULL, 16);
            str += 3;
        } else if (*str == '+') {
            *p++ = ' ';
            str++;
        } else {
            *p++ = *str++;
        }
    }
    *p = '\0';
    
    return decoded;
}

char* http_build_query(const char* base_url, const char** params) {
    if (!base_url) return NULL;
    
    size_t url_len = strlen(base_url);
    size_t query_len = 0;
    
    // 计算查询字符串长度
    if (params) {
        for (int i = 0; params[i] && params[i+1]; i += 2) {
            char* enc_key = http_url_encode(params[i]);
            char* enc_val = http_url_encode(params[i+1]);
            if (enc_key && enc_val) {
                query_len += strlen(enc_key) + strlen(enc_val) + 2;  // key=val&
            }
            free(enc_key);
            free(enc_val);
        }
    }
    
    if (query_len == 0) return strdup(base_url);
    
    // 构建完整 URL
    char* url = (char*)malloc(url_len + query_len + 2);
    if (!url) return NULL;
    
    strcpy(url, base_url);
    char* p = url + url_len;
    
    // 判断是否已有查询参数
    if (strchr(base_url, '?')) {
        *p++ = '&';
    } else {
        *p++ = '?';
    }
    
    // 添加参数
    bool first = true;
    if (params) {
        for (int i = 0; params[i] && params[i+1]; i += 2) {
            char* enc_key = http_url_encode(params[i]);
            char* enc_val = http_url_encode(params[i+1]);
            if (enc_key && enc_val) {
                if (!first) *p++ = '&';
                p += sprintf(p, "%s=%s", enc_key, enc_val);
                first = false;
            }
            free(enc_key);
            free(enc_val);
        }
    }
    *p = '\0';
    
    return url;
}

#else
// ==================== 无 curl 时的存根实现 ====================

bool http_client_init(void) {
    log_error("HTTP client not available: curl not compiled in");
    return false;
}

void http_client_cleanup(void) {}

// HttpHeaders 存根实现
typedef struct HttpHeaders {
    int dummy;
} HttpHeaders;

HttpHeaders* http_headers_new(void) { return NULL; }
void http_headers_add(HttpHeaders* headers, const char* key, const char* value) {
    (void)headers; (void)key; (void)value;
}
void http_headers_free(HttpHeaders* headers) { (void)headers; }
const char** http_headers_to_array(HttpHeaders* headers) {
    (void)headers;
    return NULL;
}

HttpResponse* http_get(const char* url) {
    (void)url;
    log_error("HTTP client not available: curl not compiled in");
    return NULL;
}

HttpResponse* http_get_with_headers(const char* url, HttpHeaders* headers) {
    (void)url; (void)headers;
    log_error("HTTP client not available: curl not compiled in");
    return NULL;
}

HttpResponse* http_post(const char* url, const char* json_body) {
    (void)url; (void)json_body;
    log_error("HTTP client not available: curl not compiled in");
    return NULL;
}

HttpResponse* http_post_json_with_headers(const char* url, const char* json_body, HttpHeaders* headers) {
    (void)url; (void)json_body; (void)headers;
    log_error("HTTP client not available: curl not compiled in");
    return NULL;
}

HttpResponse* http_post_data(const char* url, const char* body, const char* content_type) {
    (void)url; (void)body; (void)content_type;
    log_error("HTTP client not available: curl not compiled in");
    return NULL;
}

HttpResponse* http_request(const HttpRequest* req) {
    (void)req;
    log_error("HTTP client not available: curl not compiled in");
    return NULL;
}

HttpResponse* http_request_stream(const HttpRequest* req, HttpStreamCallback callback, void* user_data) {
    (void)req; (void)callback; (void)user_data;
    log_error("HTTP client not available: curl not compiled in");
    return NULL;
}

void http_response_free(HttpResponse* resp) {
    (void)resp;
}

char* http_response_json_string(HttpResponse* resp, const char* key) {
    (void)resp; (void)key;
    return NULL;
}

int http_response_json_int(HttpResponse* resp, const char* key, int default_val) {
    (void)resp; (void)key;
    return default_val;
}

char* http_url_encode(const char* str) {
    (void)str;
    return NULL;
}

char* http_url_decode(const char* str) {
    (void)str;
    return NULL;
}

char* http_build_query(const char* base_url, const char** params) {
    (void)base_url; (void)params;
    return NULL;
}

#endif // HAVE_CURL
