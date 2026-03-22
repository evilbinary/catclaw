# CatClaw 配置文档

配置文件位于 `~/.catclaw/config.json`，使用 JSON 格式。

## 配置结构

```json
{
  "models": {
    "list": [...],
    "default": "model-name"
  },
  "gateway": {...},
  "channels": [...],
  "session": {...},
  "logging": {...},
  "compaction": {...},
  "agent": {...}
}
```

---

## 模型配置 (models)

支持配置多个 AI 模型，并可以动态切换。

### 多模型配置

```json
{
  "models": {
    "list": [
      {
        "name": "gpt4",
        "provider": "openai",
        "model_name": "gpt-4o",
        "api_key": "sk-xxx",
        "base_url": "https://api.openai.com/v1",
        "max_context_tokens": 8192,
        "max_tokens": 2048,
        "temperature": 0.7,
        "timeout_seconds": 60,
        "stream": true
      },
      {
        "name": "claude",
        "provider": "anthropic",
        "model_name": "claude-3-sonnet-20240229",
        "api_key": "sk-ant-xxx",
        "max_context_tokens": 100000,
        "temperature": 0.7
      },
      {
        "name": "gemini",
        "provider": "gemini",
        "model_name": "gemini-1.5-pro",
        "api_key": "xxx"
      },
      {
        "name": "local",
        "provider": "ollama",
        "model_name": "llama3",
        "base_url": "http://localhost:11434"
      }
    ],
    "default": "gpt4"
  }
}
```

### 模型配置字段

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `name` | string | 是 | 模型标识名称，用于切换 |
| `provider` | string | 是 | 提供商: `openai`, `anthropic`, `gemini`, `ollama` |
| `model_name` | string | 是 | 模型名称 |
| `api_key` | string | 视情况 | API 密钥 |
| `base_url` | string | 否 | API 基础 URL (用于代理或私有部署) |
| `max_context_tokens` | int | 否 | 最大上下文 token 数，默认 4096 |
| `max_tokens` | int | 否 | 最大生成 token 数，默认 1024 |
| `temperature` | float | 否 | 温度参数，默认 0.7 |
| `timeout_seconds` | int | 否 | 超时时间(秒)，默认 30 |
| `stream` | bool | 否 | 是否启用流式响应，默认 true |

### 模型切换命令

```
/model gpt4          # 切换到指定模型
/model list          # 列出所有可用模型
```

---

## 渠道配置 (channels)

支持配置多个消息渠道，可以同时接入多个飞书、Telegram、Discord 等渠道。

### 完整示例

```json
{
  "channels": {
    "list": [
      {
        "id": "feishu-webhook-1",
        "name": "飞书通知机器人",
        "type": "feishu",
        "enabled": true,
        "webhook_url": "https://open.feishu.cn/open-apis/bot/v2/hook/xxx"
      },
      {
        "id": "feishu-api-1",
        "name": "飞书API机器人",
        "type": "feishu",
        "enabled": true,
        "app_id": "cli_xxx",
        "app_secret": "xxx",
        "receive_id": "ou_xxx",
        "receive_id_type": "open_id"
      },
      {
        "id": "telegram-1",
        "name": "Telegram Bot",
        "type": "telegram",
        "enabled": true,
        "bot_token": "123456:ABC-xxx",
        "chat_id": "-1001234567890"
      },
      {
        "id": "discord-1",
        "name": "Discord Bot",
        "type": "discord",
        "enabled": true,
        "bot_token": "xxx.xxx.xxx",
        "channel_id": "123456789012345678"
      }
    ],
    "default": "feishu-webhook-1"
  }
}
```

### 简化配置（数组格式，向后兼容）

```json
{
  "channels": [
    {
      "id": "feishu-1",
      "name": "飞书机器人",
      "type": "feishu",
      "webhook_url": "https://open.feishu.cn/open-apis/bot/v2/hook/xxx"
    }
  ]
}
```

### 通用配置字段

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `id` | string | 是 | 渠道唯一标识符 |
| `name` | string | 否 | 显示名称 |
| `type` | string | 是 | 渠道类型: `feishu`, `telegram`, `discord`, `webchat` |
| `enabled` | bool | 否 | 是否启用，默认 true |

### 飞书渠道配置

飞书支持两种模式：

#### 1. Webhook 模式 (简单)

适用于机器人接收消息并回复的场景，配置简单：

```json
{
  "id": "feishu-webhook",
  "type": "feishu",
  "webhook_url": "https://open.feishu.cn/open-apis/bot/v2/hook/xxx"
}
```

#### 2. API 模式 (完整)

可以主动发送消息给指定用户或群组：

```json
{
  "id": "feishu-api",
  "type": "feishu",
  "app_id": "cli_xxx",
  "app_secret": "xxx",
  "receive_id": "ou_xxx",
  "receive_id_type": "open_id"
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `app_id` | string | 飞书应用 ID |
| `app_secret` | string | 飞书应用密钥 |
| `receive_id` | string | 接收消息的用户/群组 ID |
| `receive_id_type` | string | ID 类型: `open_id`, `user_id`, `union_id`, `chat_id` |
| `webhook_url` | string | 自定义机器人 Webhook 地址 |
| `ws_enabled` | bool | 是否启用 WebSocket 事件订阅 (默认 false) |
| `ws_domain` | string | WebSocket 连接域名 (可选，默认 `wss://open.feishu.cn`) |
| `ws_ping_interval` | int | 心跳间隔(秒)，默认 120 |
| `ws_reconnect_interval` | int | 重连间隔(秒)，默认 120 |
| `ws_max_reconnect` | int | 最大重连次数，-1 表示无限 |

#### WebSocket 事件订阅

启用 `ws_enabled = true` 后，系统会自动建立 WebSocket 连接订阅飞书事件，无需配置公网回调 URL。

```json
{
  "id": "feishu-main",
  "type": "feishu",
  "app_id": "cli_xxx",
  "app_secret": "xxx",
  "ws_enabled": true
}
```

| 特性 | WebSocket 订阅 | HTTP Webhook |
|------|---------------|--------------|
| 接收消息 | 实时推送 | 需要配置事件订阅 |
| 网络要求 | 支持内网穿透 | 需要公网 IP |
| 配置复杂度 | 简单 | 需要配置回调 URL |
| 适用场景 | 本地开发、内网环境 | 生产环境 |

### Telegram 渠道配置

```json
{
  "id": "telegram-bot",
  "type": "telegram",
  "bot_token": "123456:ABC-xxx",
  "chat_id": "-1001234567890"
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `bot_token` | string | Bot Token (从 @BotFather 获取) |
| `chat_id` | string | 目标聊天 ID (用户、群组或频道) |

### Discord 渠道配置

支持 Bot 和 Webhook 两种方式：

#### Bot 模式

```json
{
  "id": "discord-bot",
  "type": "discord",
  "bot_token": "xxx.xxx.xxx",
  "channel_id": "123456789012345678"
}
```

#### Webhook 模式

```json
{
  "id": "discord-webhook",
  "type": "discord",
  "webhook_url": "https://discord.com/api/webhooks/xxx/xxx"
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `bot_token` | string | Bot Token |
| `channel_id` | string | 目标频道 ID |
| `webhook_url` | string | Webhook URL |

### 渠道管理命令

```
/channel list              # 列出所有渠道
/channel enable <id>       # 启用渠道
/channel disable <id>      # 禁用渠道
/channel connect <id>      # 连接渠道
/channel disconnect <id>   # 断开渠道
```

---

## 网关配置 (gateway)

```json
{
  "gateway": {
    "port": 18789,
    "browser_enabled": false,
    "http_api_key": "your-api-key",
    "http_auth_enabled": true,
    "http_server_enabled": true,
    "websocket_enabled": true
  }
}
```

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `port` | int | 18789 | WebSocket/HTTP 网关端口 |
| `browser_enabled` | bool | false | 是否启用浏览器界面 |
| `http_api_key` | string | - | API 授权密钥 |
| `http_auth_enabled` | bool | false | 是否启用 HTTP 授权 |
| `http_server_enabled` | bool | false | 是否启用 HTTP API 服务器 |
| `websocket_enabled` | bool | false | 是否启用 WebSocket 服务器 |

> **注意**: `http_server_enabled` 和 `websocket_enabled` 默认为 `false`，需要显式配置 `true` 才会启用相应服务。

---

## 会话配置 (session)

```json
{
  "session": {
    "max_sessions": 100,
    "auto_save": true,
    "default_session_key": "default",
    "max_history_per_session": 1000,
    "context_history_limit": 5
  }
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `max_sessions` | int | 最大会话数量 |
| `auto_save` | bool | 是否自动保存会话 |
| `default_session_key` | string | 默认会话标识 |
| `max_history_per_session` | int | 每个会话最大历史消息数 |
| `context_history_limit` | int | 构建上下文时加载的历史消息限制 |

---

## 日志配置 (logging)

```json
{
  "logging": {
    "level": "info",
    "file": "/var/log/catclaw.log",
    "console_output": true
  }
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `level` | string | 日志级别: `debug`, `info`, `warn`, `error`, `fatal` |
| `file` | string | 日志文件路径 |
| `console_output` | bool | 是否输出到控制台 |

---

## 压缩配置 (compaction)

```json
{
  "compaction": {
    "enabled": true,
    "threshold": 3000
  }
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `enabled` | bool | 是否启用上下文压缩 |
| `threshold` | int | 触发压缩的 token 阈值 |

---

## Agent 配置 (agent)

```json
{
  "agent": {
    "system_prompt": "你是一个有帮助的AI助手。"
  }
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `system_prompt` | string | 系统提示词 |

---

## 完整配置示例

```json
{
  "models": {
    "list": [
      {
        "name": "gpt4",
        "provider": "openai",
        "model_name": "gpt-4o",
        "api_key": "sk-xxx"
      },
      {
        "name": "claude",
        "provider": "anthropic",
        "model_name": "claude-3-sonnet-20240229",
        "api_key": "sk-ant-xxx"
      }
    ],
    "default": "gpt4"
  },
  
  "channels": [
    {
      "id": "feishu-main",
      "name": "主飞书机器人",
      "type": "feishu",
      "enabled": true,
      "webhook_url": "https://open.feishu.cn/open-apis/bot/v2/hook/xxx"
    },
    {
      "id": "telegram-main",
      "name": "Telegram Bot",
      "type": "telegram",
      "enabled": true,
      "bot_token": "xxx:xxx",
      "chat_id": "-100xxx"
    }
  ],
  
  "gateway": {
    "port": 18789,
    "http_api_key": "secure-api-key-here",
    "http_auth_enabled": true,
    "http_server_enabled": true,
    "websocket_enabled": true
  },
  
  "session": {
    "max_sessions": 100,
    "auto_save": true,
    "context_history_limit": 10
  },
  
  "logging": {
    "level": "info",
    "console_output": true
  },
  
  "compaction": {
    "enabled": true,
    "threshold": 3000
  },
  
  "agent": {
    "system_prompt": "你是一个有帮助的AI助手，请用中文回答问题。"
  }
}
```

---

## 命令行选项

```bash
# 启动时指定日志级别
./catclaw --log-level debug
./catclaw -l debug

# 查看帮助
./catclaw --help
./catclaw -h
```

---

## 配置管理命令

在 CatClaw 交互界面中：

```
/config list              # 显示当前配置
/config get <key>         # 获取配置值
/config set <key> <value> # 设置配置值
```
