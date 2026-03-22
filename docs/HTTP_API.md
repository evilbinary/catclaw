# CatClaw HTTP API 文档

## 概述

CatClaw 提供 HTTP REST API，用于外部程序集成和远程控制。

- **默认端口**: 8080
- **Content-Type**: `application/json`
- **CORS**: 已启用，允许跨域请求

## 授权认证

### 配置 API Key

在配置文件 `~/.catclaw/config.json` 中添加：

```json
{
  "gateway": {
    "port": 18789,
    "http_api_key": "your-secret-api-key"
  }
}
```

配置 `http_api_key` 后，所有 HTTP API 请求都需要授权。

### 授权方式

支持两种方式传递 API Key：

**方式 1: Authorization Header (推荐)**
```bash
curl -H "Authorization: Bearer your-secret-api-key" \
  http://localhost:8080/chat
```

**方式 2: X-API-Key Header**
```bash
curl -H "X-API-Key: your-secret-api-key" \
  http://localhost:8080/chat
```

### 未授权响应

如果未提供正确的 API Key，将返回：

```json
{
  "error": "Unauthorized",
  "status": 401
}
```

响应头包含：
```
WWW-Authenticate: Bearer
```

## 通用响应格式

### 成功响应
```json
{
  "status": "success",
  "data": { ... }
}
```

### 错误响应
```json
{
  "error": "错误描述",
  "status": 400
}
```

---

## API 端点

### 健康检查

**GET** `/health`

检查服务状态。

**响应示例**:
```json
{
  "status": "healthy",
  "version": "1.0.0",
  "model": "openai/gpt-4o"
}
```

---

### 模型管理

#### 列出可用模型

**GET** `/models`

获取所有支持的模型列表。

**响应示例**:
```json
{
  "models": [
    "openai/gpt-4o",
    "openai/gpt-3.5-turbo",
    "anthropic/claude-3-opus-20240229",
    "anthropic/claude-3-sonnet-20240229",
    "gemini/gemini-1.5-pro",
    "llama/llama3-70b"
  ],
  "current": "openai/gpt-4o"
}
```

#### 切换模型

**POST** `/model/switch`

切换当前使用的模型。

**请求体**:
```json
{
  "model": "anthropic/claude-3-opus-20240229"
}
```

**响应示例**:
```json
{
  "status": "success",
  "model": "anthropic/claude-3-opus-20240229",
  "message": "Model switched successfully"
}
```

---

### 聊天接口

#### 发送消息

**POST** `/chat`

向 AI 发送消息。

**请求体**:
```json
{
  "message": "你好，请介绍一下自己",
  "session_id": "default",
  "stream": false
}
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| message | string | 是 | 用户消息内容 |
| session_id | string | 否 | 会话 ID，默认 "default" |
| stream | boolean | 否 | 是否流式响应，默认 false |

**响应示例**:
```json
{
  "status": "queued",
  "session_id": "default",
  "message": "Message sent successfully"
}
```

#### 流式聊天

**POST** `/chat` (stream=true)

设置 `stream: true` 或添加请求头 `Accept: text/event-stream` 启用流式响应。

**响应格式**: Server-Sent Events (SSE)

```
event: status
data: Message processing...

event: status
data: Message queued

event: done
data: [DONE]
```

---

### 工具接口

#### 列出可用工具

**GET** `/tools`

获取所有注册的工具列表。

**响应示例**:
```json
{
  "tools": [
    {
      "name": "read_file",
      "description": "读取文件内容"
    },
    {
      "name": "write_file",
      "description": "写入文件"
    },
    {
      "name": "execute_command",
      "description": "执行 shell 命令"
    }
  ]
}
```

#### 执行工具

**POST** `/tools/execute`

执行指定的工具。

**请求体**:
```json
{
  "name": "read_file",
  "args": {
    "path": "/path/to/file.txt"
  }
}
```

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| name | string | 是 | 工具名称 |
| args | object | 否 | 工具参数 |

**响应示例**:
```json
{
  "tool": "read_file",
  "status": "success",
  "result": "文件内容..."
}
```

**错误响应**:
```json
{
  "tool": "read_file",
  "status": "error",
  "error": "File not found"
}
```

---

### 会话管理

#### 列出会话

**GET** `/sessions`

获取所有会话列表。

**响应示例**:
```json
{
  "sessions": [
    {
      "id": "default",
      "message_count": 10
    },
    {
      "id": "session-123",
      "message_count": 5
    }
  ]
}
```

#### 清除会话

**POST** `/session/clear`

清除指定会话的历史记录。

**请求体**:
```json
{
  "session_id": "default"
}
```

**响应示例**:
```json
{
  "status": "success",
  "session_id": "default",
  "message": "Session cleared"
}
```

---

## 使用示例

### cURL

```bash
# 设置 API Key (如果配置了授权)
API_KEY="your-secret-api-key"

# 健康检查
curl http://localhost:8080/health

# 发送消息 (需要授权时)
curl -X POST http://localhost:8080/chat \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer $API_KEY" \
  -d '{"message":"你好"}'

# 列出工具
curl -H "Authorization: Bearer $API_KEY" \
  http://localhost:8080/tools

# 执行工具
curl -X POST http://localhost:8080/tools/execute \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer $API_KEY" \
  -d '{"name":"time"}'

# 使用 X-API-Key 方式授权
curl -X POST http://localhost:8080/tools/execute \
  -H "Content-Type: application/json" \
  -H "X-API-Key: $API_KEY" \
  -d '{"name":"read_file","args":{"path":"example.txt"}}'

# 流式请求
curl -N http://localhost:8080/chat \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer $API_KEY" \
  -d '{"message":"讲个故事","stream":true}'
```

### JavaScript

```javascript
// 发送消息
const response = await fetch('http://localhost:8080/chat', {
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({ message: '你好' })
});
const data = await response.json();

// 流式请求
const eventSource = new EventSource('http://localhost:8080/chat');
eventSource.onmessage = (event) => {
  console.log(event.data);
};
```

### Python

```python
import requests

# 发送消息
response = requests.post('http://localhost:8080/chat', 
    json={'message': '你好'})
print(response.json())

# 流式请求
response = requests.post('http://localhost:8080/chat',
    json={'message': '讲个故事', 'stream': True},
    stream=True)
for line in response.iter_lines():
    if line:
        print(line.decode())
```

---

## HTTP 客户端 API (C 语言)

如果需要在代码中调用外部 HTTP API，可使用 `http_client` 模块：

### 头文件
```c
#include "common/http_client.h"
```

### 初始化
```c
http_client_init();  // 程序启动时调用
http_client_cleanup(); // 程序退出时调用
```

### GET 请求
```c
HttpResponse* resp = http_get("https://api.example.com/data");
if (resp && resp->success) {
    printf("Status: %d\n", resp->status_code);
    printf("Body: %s\n", resp->body);
}
http_response_free(resp);
```

### POST 请求
```c
HttpResponse* resp = http_post("https://api.example.com/chat",
    "{\"message\":\"hello\"}");
char* result = http_response_json_string(resp, "result");
http_response_free(resp);
```

### 高级配置
```c
HttpRequest req = {
    .url = "https://api.example.com/data",
    .method = "POST",
    .body = "{\"key\":\"value\"}",
    .content_type = "application/json",
    .timeout_sec = 30,
    .follow_redirects = true
};
HttpResponse* resp = http_request(&req);
```

### 流式请求
```c
bool my_callback(const char* data, size_t len, void* user_data) {
    printf("Received: %.*s\n", (int)len, data);
    return true; // 返回 false 中断
}

http_request_stream(&(HttpRequest){
    .url = "https://api.example.com/stream"
}, my_callback, NULL);
```

### 工具函数
```c
// URL 编码
char* encoded = http_url_encode("hello world");
// encoded = "hello%20world"

// 构建查询字符串
const char* params[] = {"key1", "value1", "key2", "value2", NULL};
char* url = http_build_query("https://api.example.com", params);
// url = "https://api.example.com?key1=value1&key2=value2"
```
