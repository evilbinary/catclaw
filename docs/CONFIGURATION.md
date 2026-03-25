# CatClaw 配置文档

## 概述

CatClaw 的配置文件位于 `~/.catclaw/config.json`。如果不存在，程序会自动创建默认配置。

## 配置文件位置

- **配置文件路径**: `~/.catclaw/config.json`
- **日志文件路径**: `~/.catclaw/catclaw.log`
- **工作空间路径**: `~/.catclaw/workspace`
- **会话存储路径**: `~/.catclaw/workspace/sessions`

## 完整配置示例

```json
{
  "models": [
    {
      "name": "claude-3-opus",
      "provider": "anthropic",
      "base_url": "https://api.anthropic.com",
      "model_name": "claude-3-opus-20240229",
      "api_key": "YOUR_ANTHROPIC_API_KEY",
      "temperature": 0.7,
      "max_tokens": 4096,
      "stream": true
    },
    {
      "name": "gemini-pro",
      "provider": "gemini",
      "base_url": "https://generativelanguage.googleapis.com/v1beta",
      "model_name": "gemini-pro",
      "api_key": "YOUR_GEMINI_API_KEY",
      "temperature": 0.7,
      "max_tokens": 4096,
      "stream": true
    },
    {
      "name": "local-llama",
      "provider": "ollama",
      "base_url": "http://localhost:11434/api",
      "model_name": "llama3.2",
      "api_key": "",
      "temperature": 0.7,
      "max_tokens": 4096,
      "stream": true
    },
    {
      "name": "openai-gpt4",
      "provider": "openai",
      "base_url": "https://api.openai.com/v1",
      "model_name": "gpt-4",
      "api_key": "YOUR_OPENAI_API_KEY",
      "temperature": 0.7,
      "max_tokens": 4096,
      "stream": true
    }
  ],
  "channels": [
    {
      "id": "feishu-api",
      "name": "飞书API",
      "type": "feishu",
      "enabled": false,
      "app_id": "your-feishu-app-id",
      "app_secret": "your-feishu-app-secret",
      "receive_id": "",
      "receive_id_type": "open_id",
      "ws_enabled": false,
      "ws_ping_interval": 120,
      "ws_reconnect_interval": 120,
      "ws_max_reconnect": -1,
      "stream_mode": true,
      "stream_speed": 30
    },
    {
      "id": "weixin_1",
      "name": "WeChat Bot",
      "type": "weixin",
      "enabled": true,
      "api_key": "your-weixin-bot-token",
      "stream_mode": true
    }
  ],
  "gateway": {
    "port": 18789,
    "browser_enabled": false,
    "debug": false
  },
  "agent": {
    "system_prompt": ""
  },
  "workspace": {
    "path": "~/.catclaw/workspace"
  },
  "session": {
    "max_sessions": 100,
    "auto_save": true,
    "default_session_key": "default",
    "max_history_per_session": 1000,
    "context_history_limit": 5
  },
  "logging": {
    "level": "debug",
    "file": "~/.catclaw/logs/catclaw.log",
    "console_output": true
  },
  "compaction": {
    "enabled": true,
    "threshold": 3000
  }
}
```

## 配置项详解

### 模型配置 (`model`)

当前使用的模型配置。

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `name` | string | 是 | 模型显示名称 |
| `provider` | string | 是 | 模型提供商：`anthropic`, `openai`, `gemini`, `ollama` |
| `model_name` | string | 是 | 实际模型名称 |
| `api_key` | string | 是 | API 密钥 |
| `base_url` | string | 是 | API 基础 URL |
| `temperature` | float | 否 | 温度参数（0-1），默认 0.7 |
| `max_tokens` | int | 否 | 最大生成 token 数，默认 4096 |
| `stream` | bool | 否 | 是否启用流式输出，默认 true |

### 模型列表 (`models`)

支持的模型列表，可通过 `/model` 命令切换。

配置格式同 `model` 对象。

### 会话配置 (`session`)

管理会话和历史记录的配置。

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `max_sessions` | int | 10 | 最大会话数量 |
| `auto_save` | bool | true | 是否自动保存会话到磁盘 |
| `default_session_key` | string | "default" | 默认会话标识符 |
| `max_history_per_session` | int | 100 | 每个会话的最大历史消息数 |
| `context_history_limit` | int | 5 | 构建上下文时使用的历史消息数量 |

### 网关配置 (`gateway`)

HTTP API 网关的配置。

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `port` | int | 8080 | HTTP 服务器端口 |
| `debug` | bool | false | 是否启用调试模式 |

### 工作空间配置 (`workspace`)

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `path` | string | `~/.catclaw/workspace` | 工作空间路径，支持 `~` 展开 |

### 渠道配置 (`channels`)

支持的聊天渠道列表，用于接收和发送消息。

#### 微信渠道 (`type: "weixin"`)

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `id` | string | 是 | 渠道唯一标识符 |
| `name` | string | 是 | 渠道显示名称 |
| `type` | string | 是 | 必须为 `"weixin"` |
| `api_key` | string | 是 | 微信 Bot Token（扫码登录后自动生成） |
| `stream_mode` | bool | 否 | 是否启用流式消息，默认 true |

**配置方法**：
1. 启动程序，按照提示扫码登录微信
2. 扫码成功后会自动生成 `bot_token`
3. 将 `bot_token` 复制到配置文件的 `api_key` 字段

#### Telegram 渠道 (`type: "telegram"`)

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `id` | string | 是 | 渠道唯一标识符 |
| `name` | string | 是 | 渠道显示名称 |
| `type` | string | 是 | 必须为 `"telegram"` |
| `api_key` | string | 是 | Telegram Bot Token（从 @BotFather 获取） |
| `stream_mode` | bool | 否 | 是否启用流式消息，默认 true |

**配置方法**：
1. 在 Telegram 中找到 @BotFather
2. 创建新 Bot，获取 Bot Token
3. 将 Token 复制到配置文件的 `api_key` 字段

#### 飞书渠道 (`type: "feishu"`)

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `id` | string | 是 | 渠道唯一标识符 |
| `name` | string | 是 | 渠道显示名称 |
| `type` | string | 是 | 必须为 `"feishu"` |
| `api_key` | string | 是 | 飞书 App ID |
| `api_secret` | string | 是 | 飞书 App Secret（从飞书开放平台获取） |
| `stream_mode` | bool | 否 | 是否启用流式消息，默认 true |

**配置方法**：
1. 访问飞书开放平台（https://open.feishu.cn/）
2. 创建企业自建应用
3. 获取 App ID 和 App Secret
4. 配置权限：发送消息、接收消息等
5. 将信息复制到配置文件

### 日志配置 (`logging`)

日志系统的配置。

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `level` | string | "info" | 日志级别：`debug`, `info`, `warn`, `error`, `fatal` |
| `file` | string | `~/.catclaw/catclaw.log` | 日志文件路径 |
| `console_output` | bool | true | 是否输出到控制台 |

### 会话压缩配置 (`compaction`)

自动压缩长会话历史以节省 token。

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `enabled` | bool | false | 是否启用压缩 |
| `threshold` | int | 3000 | 压缩阈值（token 数），超过则触发压缩 |

### Agent 配置 (`agent`)

AI 助手的行为配置。

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `system_prompt` | string | 默认系统提示词 | 自定义系统提示词，覆盖默认值 |

### 网关高级配置 (`gateway`)

HTTP API 网关的高级配置。

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `port` | int | 8080 | HTTP 服务器端口 |
| `browser_enabled` | bool | true | 是否启用浏览器功能 |
| `http_api_key` | string | - | HTTP API 授权密钥（可选） |
| `http_auth_enabled` | bool | false | 是否启用 HTTP 授权 |
| `http_server_enabled` | bool | true | 是否启用 HTTP API 服务器 |
| `websocket_enabled` | bool | false | 是否启用 WebSocket 服务器 |
| `debug` | bool | false | 是否启用调试模式 |

## 环境变量

某些配置可以通过环境变量覆盖：

| 环境变量 | 说明 |
|----------|------|
| `ANTHROPIC_API_KEY` | Anthropic API 密钥（覆盖配置文件） |
| `OPENAI_API_KEY` | OpenAI API 密钥（覆盖配置文件） |
| `CATCLAW_CONFIG` | 配置文件路径（默认 `~/.catclaw/config.json`） |
| `CATCLAW_DEBUG` | 启用调试模式（任意值） |

## 配置优先级

1. 环境变量（最高优先级）
2. 配置文件 `~/.catclaw/config.json`
3. 默认值（最低优先级）

## 示例：配置 OpenAI 模型

```json
{
  "model": {
    "name": "GPT-4",
    "provider": "openai",
    "model_name": "gpt-4",
    "api_key": "sk-xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
    "base_url": "https://api.openai.com/v1",
    "temperature": 0.7,
    "max_tokens": 4096,
    "stream": true
  }
}
```

## 示例：配置 Ollama 本地模型

```json
{
  "model": {
    "name": "Llama 3.2",
    "provider": "ollama",
    "model_name": "llama3.2",
    "api_key": "",
    "base_url": "http://localhost:11434/api",
    "temperature": 0.7,
    "max_tokens": 4096,
    "stream": true
  }
}
```

## 示例：配置多个渠道

```json
{
  "channels": [
    {
      "id": "weixin",
      "name": "微信",
      "type": "weixin",
      "api_key": "your-weixin-bot-token",
      "stream_mode": true
    },
    {
      "id": "telegram",
      "name": "Telegram",
      "type": "telegram",
      "api_key": "your-telegram-bot-token",
      "stream_mode": true
    },
    {
      "id": "feishu",
      "name": "飞书",
      "type": "feishu",
      "api_key": "your-feishu-app-id",
      "api_secret": "your-feishu-app-secret",
      "stream_mode": true
    }
  ]
}
```

## 调试配置

启用调试模式以查看详细日志：

```json
{
  "gateway": {
    "port": 8080,
    "debug": true
  }
}
```

或通过环境变量：

```bash
export CATCLAW_DEBUG=1
./catclaw
```

## 常见问题

### Q: 如何切换模型？

在程序中使用 `/model` 命令查看可用模型，然后使用 `/model <模型名或序号>` 切换。

### Q: API 密钥不生效？

检查：
1. 环境变量是否设置了密钥（优先级更高）
2. 配置文件路径是否正确
3. 密钥字段名是否正确（`api_key`）

### Q: 微信扫码后 token 不持久化？

扫码成功后，程序会显示生成的 Bot Token，请手动将其复制到配置文件的 `api_key` 字段。

### Q: 配置文件在哪里？

默认位置：`~/.catclaw/config.json`

可通过环境变量 `CATCLAW_CONFIG` 自定义路径。

### Q: 如何禁用某个渠道？

从配置文件的 `channels` 数组中删除对应的渠道配置即可。

### Q: 会话历史太大？

调整 `session.max_history_per_session` 限制单个会话的最大历史消息数。

## 技术支持

如有问题，请查看：
- 日志文件：`~/.catclaw/catclaw.log`
- GitHub Issues：[项目地址]

## 更新日志

- **v1.0** (2026-03-25): 初始配置文档
