/**
 * 飞书 WebSocket 客户端
 * 用于订阅飞书事件，接收实时消息
 */
#ifndef FEISHU_WS_H
#define FEISHU_WS_H

#include <stdbool.h>
#include "common/ws_client.h"

// 飞书 WebSocket 内部配置
typedef struct {
    char *app_id;           // 应用ID
    char *app_secret;       // 应用密钥
    char *domain;           // 飞书域名 (默认: https://open.feishu.cn)
    
    // 连接配置
    int ping_interval_sec;      // 心跳间隔(秒)，默认 120
    int reconnect_interval_sec; // 重连间隔(秒)，默认 120
    int max_reconnect_count;    // 最大重连次数，-1 表示无限
    
    // 连接状态
    char *conn_url;     // 当前连接 URL
    char *conn_id;      // 连接 ID
    char *service_id;   // 服务 ID
} FeishuWsInternalConfig;

// 飞书 WebSocket 客户端
typedef struct {
    FeishuWsInternalConfig config;
    WsClient *ws_client;
    bool running;
    void *user_data;
} FeishuWsClient;

// ==================== 生命周期管理 ====================

// 创建飞书 WebSocket 客户端
FeishuWsClient* feishu_ws_create(const char *app_id, const char *app_secret);

// 销毁飞书 WebSocket 客户端
void feishu_ws_destroy(FeishuWsClient *client);

// ==================== 连接管理 ====================

// 启动连接 (后台线程运行)
bool feishu_ws_start(FeishuWsClient *client);

// 停止连接
void feishu_ws_stop(FeishuWsClient *client);

// 检查是否已连接
bool feishu_ws_is_connected(FeishuWsClient *client);

// ==================== 配置 ====================

// 设置飞书域名
void feishu_ws_set_domain(FeishuWsClient *client, const char *domain);

// 设置心跳间隔
void feishu_ws_set_ping_interval(FeishuWsClient *client, int seconds);

// 设置重连配置
void feishu_ws_set_reconnect(FeishuWsClient *client, int interval_sec, int max_count);

// 设置用户数据
void feishu_ws_set_user_data(FeishuWsClient *client, void *user_data);

// ==================== 工具函数 ====================

// 获取 WebSocket 连接端点 URL
// 返回值需要 free
char* feishu_ws_get_endpoint(const char *app_id, const char *app_secret, const char *domain);

#endif // FEISHU_WS_H
