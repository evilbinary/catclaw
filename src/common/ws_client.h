/**
 * WebSocket Client - 用于连接外部 WebSocket 服务
 * 支持飞书等平台的 WebSocket 长连接
 */
#ifndef WS_CLIENT_H
#define WS_CLIENT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// WebSocket 客户端状态
typedef enum {
    WS_CLIENT_DISCONNECTED,
    WS_CLIENT_CONNECTING,
    WS_CLIENT_CONNECTED,
    WS_CLIENT_CLOSING,
    WS_CLIENT_ERROR
} WsClientState;

// WebSocket 客户端配置
typedef struct {
    const char *url;              // WebSocket URL (ws:// or wss://)
    const char *origin;           // Origin header (可选)
    const char **headers;         // 额外请求头 {"Header: Value", NULL}
    int ping_interval_sec;        // 心跳间隔(秒)，0 表示禁用
    int reconnect_interval_sec;   // 重连间隔(秒)
    int max_reconnect_count;      // 最大重连次数，-1 表示无限
    int recv_buffer_size;         // 接收缓冲区大小
    int connect_timeout_sec;      // 连接超时(秒)
} WsClientConfig;

// 前向声明
typedef struct WsClient WsClient;

// 消息回调函数类型
// 返回 true 继续接收，false 断开连接
typedef bool (*WsClientMessageCallback)(WsClient *client, const char *data, size_t len, void *user_data);

// 连接状态变化回调
typedef void (*WsClientStateCallback)(WsClient *client, WsClientState state, void *user_data);

// WebSocket 客户端结构
struct WsClient {
    int socket;
    WsClientState state;
    char *url;
    char *host;
    char *path;
    int port;
    bool use_ssl;
    void *ssl;        // SSL* (OpenSSL)
    void *ssl_ctx;    // SSL_CTX* (OpenSSL)
    char *recv_buffer;
    size_t recv_buffer_size;
    size_t recv_buffer_len;
    char *send_buffer;
    size_t send_buffer_size;
    
    // 回调
    WsClientMessageCallback on_message;
    WsClientStateCallback on_state_change;
    void *user_data;
    
    // 配置
    int ping_interval_sec;
    int reconnect_interval_sec;
    int max_reconnect_count;
    int connect_timeout_sec;
    
    // 线程控制
    bool running;
    void *thread;  // pthread_t cast to void*
};

// ==================== 生命周期管理 ====================

// 创建 WebSocket 客户端
WsClient* ws_client_create(const WsClientConfig *config);

// 销毁 WebSocket 客户端
void ws_client_destroy(WsClient *client);

// ==================== 连接管理 ====================

// 连接到服务器 (同步)
bool ws_client_connect(WsClient *client);

// 断开连接
void ws_client_disconnect(WsClient *client);

// 启动事件循环 (在后台线程运行)
bool ws_client_start(WsClient *client);

// 停止事件循环
void ws_client_stop(WsClient *client);

// ==================== 消息发送 ====================

// 发送文本消息
bool ws_client_send_text(WsClient *client, const char *text);

// 发送二进制消息
bool ws_client_send_binary(WsClient *client, const void *data, size_t len);

// 发送 Ping 帧
bool ws_client_send_ping(WsClient *client, const void *data, size_t len);

// 发送 Pong 帧
bool ws_client_send_pong(WsClient *client, const void *data, size_t len);

// ==================== 状态查询 ====================

// 获取当前状态
WsClientState ws_client_get_state(WsClient *client);

// 检查是否已连接
bool ws_client_is_connected(WsClient *client);

// ==================== 回调设置 ====================

// 设置消息回调
void ws_client_set_message_callback(WsClient *client, 
                                     WsClientMessageCallback callback, 
                                     void *user_data);

// 设置状态变化回调
void ws_client_set_state_callback(WsClient *client,
                                   WsClientStateCallback callback,
                                   void *user_data);

// ==================== 工具函数 ====================

// 解析 WebSocket URL
// 返回值需要 free
char* ws_client_parse_url(const char *url, char **host, char **path, int *port, bool *use_ssl);

#endif // WS_CLIENT_H
