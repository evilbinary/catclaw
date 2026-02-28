# CatClaw - C version

CatClaw是基于OpenClaw功能的C语言实现，一个功能完整的个人AI助手系统。

## 项目结构

```
catclaw/
├── src/
│   ├── main.c          # 主入口文件
│   ├── config.h        # 配置模块头文件
│   ├── config.c        # 配置模块实现
│   ├── gateway.h       # 网关模块头文件
│   ├── gateway.c       # 网关模块实现
│   ├── channels.h      # 渠道模块头文件
│   ├── channels.c      # 渠道模块实现
│   ├── agent.h         # 代理模块头文件
│   ├── agent.c         # 代理模块实现
│   ├── ai_model.h      # AI模型API头文件
│   ├── ai_model.c      # AI模型API实现
│   ├── websocket.h     # WebSocket服务器头文件
│   ├── websocket.c     # WebSocket服务器实现
│   ├── cJSON.h         # JSON解析库头文件
│   └── cJSON.c         # JSON解析库实现
├── Makefile           # 构建文件
├── README.md          # 项目说明
└── TESTING.md         # 测试指南
```

## 功能特点

- **配置管理**：基于JSON的配置系统，支持从config.json文件加载配置
- **网关服务**：完整的WebSocket服务器实现，支持实时通信
- **渠道管理**：支持多种消息渠道，包括WebChat（已实现）、WhatsApp、Telegram、Slack、Discord、Signal（待实现）
- **AI代理**：集成OpenAI和Anthropic等AI模型API
- **命令行界面**：交互式命令行，支持多种命令
- **线程支持**：网关服务器在后台线程运行

## 编译和运行

### 前提条件

- C编译器（如gcc、MSVC等）
- libcurl库（用于AI模型API集成）
- pthread库（用于线程支持）
- Winsock库（用于Windows系统的网络功能）

### 编译

使用gcc编译：

```bash
gcc -Wall -Wextra -O2 src/*.c -o catclaw -lcurl -lpthread -lws2_32
```

或使用Makefile：

```bash
make
```

### 配置

创建配置文件 `~/.catclaw/config.json`：

```json
{
  "model": "anthropic/claude-3-opus-20240229",
  "gateway_port": 18789,
  "workspace_dir": "~/.catclaw/workspace",
  "browser_enabled": false
}
```

设置API密钥环境变量：

### For Anthropic

```bash
export ANTHROPIC_API_KEY="your-api-key"
```

### For OpenAI

```bash
export OPENAI_API_KEY="your-api-key"
```

### 运行

```bash
./catclaw
```

## 命令说明

- `help` - 显示帮助信息
- `status` - 显示系统状态
- `message <text>` - 发送消息给AI代理
- `gateway start` - 启动网关服务器
- `gateway stop` - 停止网关服务器
- `gateway status` - 显示网关状态
- `exit` - 退出程序

## 实现状态

- [x] 基本项目结构
- [x] JSON解析库集成
- [x] 基于JSON的配置管理
- [x] WebSocket服务器实现
- [x] AI模型API集成（OpenAI、Anthropic）
- [x] 渠道管理（WebChat已实现）
- [x] 命令行界面
- [x] 线程支持
- [ ] 其他渠道实现（WhatsApp、Telegram等）
- [ ] 浏览器控制
- [ ] Canvas支持

## WebSocket接口

CatClaw提供WebSocket接口，可用于与客户端进行实时通信：

```
ws://localhost:18789
```

### 消息格式

- 客户端发送：普通文本消息
- 服务器响应：AI模型的回复

## 基于OpenClaw

此项目基于OpenClaw的功能设计，OpenClaw是一个功能丰富的个人AI助手系统，使用TypeScript/Node.js实现。C版本的CatClaw保留了核心功能架构，同时提供了更轻量级的实现。

## 许可证

MIT License
