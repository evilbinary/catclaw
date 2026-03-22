#ifndef HTTP_API_H
#define HTTP_API_H

#include "http_server.h"
#include <stdbool.h>

// ==================== API 端点 ====================
// 
// POST /chat          - 发送消息并获取 AI 响应 (支持流式)
// GET  /health        - 健康检查
// GET  /models        - 列出可用模型
// POST /model/switch  - 切换模型
// GET  /tools         - 列出可用工具
// POST /tools/execute - 执行工具
// GET  /sessions      - 列出会话
// POST /session/clear - 清除会话

// ==================== 初始化 API ====================

// 初始化 HTTP API 服务器
// port: 监听端口
// 返回: HTTP 服务器实例
HttpServer* http_api_init(int port);

// 启动 API 服务器
bool http_api_start(HttpServer* server);

// 停止 API 服务器
void http_api_stop(HttpServer* server);

// 清理 API 服务器
void http_api_cleanup(HttpServer* server);

// ==================== 请求体结构 ====================

// Chat 请求
typedef struct {
    char* message;      // 用户消息
    char* session_id;   // 会话 ID (可选)
    bool stream;        // 是否流式响应
} ChatRequest;

// Model 切换请求
typedef struct {
    char* model;        // 模型名称 (如 "openai/gpt-4o")
} ModelSwitchRequest;

// Tool 执行请求
typedef struct {
    char* name;         // 工具名称
    char* args;         // 工具参数 (JSON 字符串)
} ToolExecuteRequest;

// ==================== JSON 解析辅助 ====================

// 解析 ChatRequest
ChatRequest* chat_request_parse(const char* json);

// 释放 ChatRequest
void chat_request_free(ChatRequest* req);

// 解析 ModelSwitchRequest
ModelSwitchRequest* model_switch_request_parse(const char* json);

// 释放 ModelSwitchRequest
void model_switch_request_free(ModelSwitchRequest* req);

// 解析 ToolExecuteRequest
ToolExecuteRequest* tool_execute_request_parse(const char* json);

// 释放 ToolExecuteRequest
void tool_execute_request_free(ToolExecuteRequest* req);

#endif // HTTP_API_H
