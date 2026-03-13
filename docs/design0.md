# CatClaw 设计文档

> C语言实现的 Mini 版 OpenClaw

---

## 目录

1. [概述](#1-概述)
2. [核心设计理念](#2-核心设计理念)
3. [系统架构](#3-系统架构)
4. [核心模块详解](#4-核心模块详解)
5. [数据结构](#5-数据结构)
6. [核心流程](#6-核心流程)
7. [文件结构](#7-文件结构)
8. [扩展指南](#8-扩展指南)

---

## 1. 概述

### 1.1 项目定位

CatClaw 是 OpenClaw 的 C 语言最小化实现，保留核心功能：
- Gateway 中心化架构
- Agent 运行时（工具调用 + LLM 集成）
- 会话管理（JSONL 持久化）
- 简单的记忆系统
- 多渠道支持（Telegram、Discord 等）

### 1.2 设计原则

| 原则 | 说明 |
|------|------|
| **极简主义** | 只保留最核心功能，避免过度设计 |
| **C 语言原生** | 纯 C 实现，最小外部依赖 |
| **模块化** | 清晰的模块划分，便于理解和扩展 |
| **文件即真相** | 配置、记忆、会话都用纯文本文件存储 |
| **跨平台** | 支持 Windows 和 Linux |

### 1.3 技术栈

- **语言**: C99
- **JSON 解析**: cJSON（内置）
- **WebSocket**: 原生实现
- **线程**: pthread
- **网络**: libcurl（可选）

---

## 2. 核心设计理念

### 2.1 Gateway 中心化

```
┌─────────────────────────────────────────────────┐
│                   Gateway                        │
│  (WebSocket 服务器 + 消息路由 + 会话管理)        │
└─────────────────────────────────────────────────┘
         │                    │                    │
         ▼                    ▼                    ▼
    ┌─────────┐         ┌─────────┐         ┌─────────┐
    │  CLI    │         │Telegram │         │ Discord │
    └─────────┘         └─────────┘         └─────────┘
```

**设计要点**:
- 单一 Gateway 进程管理所有连接
- WebSocket 作为统一通信协议
- Gateway 是会话和状态的唯一真相源

### 2.2 文件即真相

所有状态都存储在文件系统中：

```
~/.catclaw/
├── config.json          # 配置文件
├── workspace/           # Agent 工作区
│   ├── AGENTS.md        # Agent 指令
│   ├── SOUL.md          # Agent 人格
│   └── memory/          # 记忆文件
│       └── 2026-03-04.md
└── agents/
    └── main/
        ├── agent/
        │   └── auth-profiles.json
        └── sessions/
            ├── sessions.json
            └── main.jsonl
```

### 2.3 Agent 循环

```
用户消息 → 会话加载 → 上下文构建 → LLM 调用 → 工具执行 → 响应生成 → 会话持久化
     ↑                                                                      │
     └──────────────────────────────────────────────────────────────────────┘
```

---

## 3. 系统架构

### 3.1 整体架构图

```
┌─────────────────────────────────────────────────────────────────┐
│                        用户接口层                                  │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐        │
│  │  CLI 界面    │  │   Web UI     │  │  Telegram    │        │
│  └──────────────┘  └──────────────┘  └──────────────┘        │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                      Gateway 层                                   │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │              WebSocket 服务器                               │  │
│  │  - 连接管理                                                │  │
│  │  - 消息路由                                                │  │
│  │  - 事件分发                                                │  │
│  └──────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
                              │
        ┌─────────────────────┼─────────────────────┐
        ▼                     ▼                     ▼
┌───────────────┐    ┌───────────────┐    ┌───────────────┐
│   会话管理     │    │   Agent 层     │    │   渠道管理     │
│ - JSONL 存储  │    │ - 工具系统     │    │ - Telegram    │
│ - 会话隔离    │    │ - 记忆系统     │    │ - Discord     │
└───────────────┘    └───────────────┘    └───────────────┘
                              │
                              ▼
                    ┌───────────────┐
                    │   工具执行层   │
                    │ - read        │
                    │ - write       │
                    │ - exec        │
                    └───────────────┘
```

### 3.2 模块依赖关系

```
main
  ├─ config (配置)
  ├─ log (日志)
  ├─ gateway (网关)
  │   └─ websocket
  ├─ channels (渠道)
  │   ├─ telegram
  │   └─ discord
  ├─ agent (核心)
  │   ├─ context (上下文)
  │   ├─ session (会话)
  │   ├─ message (消息)
  │   ├─ tool (工具)
  │   ├─ memory (记忆)
  │   └─ skill (技能)
  ├─ ai_model (AI 模型)
  ├─ queue (消息队列)
  ├─ workspace (工作区)
  ├─ plugin (插件)
  └─ thread_pool (线程池)
```

---

## 4. 核心模块详解

### 4.1 Gateway 模块

**职责**:
- WebSocket 服务器
- 连接管理和认证
- 消息路由
- 事件分发

**核心数据结构** (见 src/gateway.h):

```c
typedef struct {
    int port;
    bool running;
    WebSocketServer ws_server;
} Gateway;
```

**核心函数**:
- `gateway_init()` - 初始化网关
- `gateway_start()` - 启动 WebSocket 服务器
- `gateway_stop()` - 停止网关

### 4.2 Agent 模块

**职责**:
- 工具注册和执行
- 记忆管理
- 状态跟踪
- 多步骤执行

**核心数据结构** (见 src/agent.h):

```c
typedef enum {
    AGENT_STATUS_IDLE,
    AGENT_STATUS_EXECUTING,
    AGENT_STATUS_PAUSED,
    AGENT_STATUS_ERROR
} AgentStatus;

typedef struct {
    char *name;
    char *description;
    char *(*execute)(const char *params);
} Tool;

typedef struct {
    char *key;
    char *value;
} MemoryEntry;

typedef struct {
    char *id;
    char *description;
    char *tool_name;
    char *params;
    char *result;
    bool completed;
} Step;

typedef struct {
    char *model;
    bool running;
    bool debug_mode;
    AgentStatus status;
    char *error_message;
    Tool *tools;
    int tool_count;
    MemoryEntry *memory;
    int memory_count;
    Step *steps;
    int step_count;
    int current_step;
} Agent;
```

**核心函数**:
- `agent_init()` - 初始化 Agent
- `agent_register_tool()` - 注册工具
- `agent_execute_tool()` - 执行工具
- `agent_memory_set/get()` - 记忆管理
- `agent_add_step()` - 添加执行步骤
- `agent_execute_steps()` - 执行多步骤

### 4.3 会话管理模块

**职责**:
- 会话创建和加载
- JSONL 持久化
- 会话隔离
- 上下文构建

**会话 JSONL 格式**:

```jsonl
{"role":"user","content":"你好"}
{"role":"assistant","content":"你好！有什么我可以帮你的吗？"}
{"role":"user","content":"读取 README.md"}
{"role":"assistant","tool_calls":[{"id":"1","type":"function","function":{"name":"read","arguments":{"path":"README.md"}}}]}
{"role":"tool","tool_call_id":"1","content":"# CatClaw\n..."}
{"role":"assistant","content":"README.md 的内容是：..."}
```

### 4.4 工具系统

**内置工具** (见 README.md):

| 工具名 | 描述 |
|--------|------|
| `calculate` | 计算器 |
| `time` | 获取时间 |
| `reverse_string` | 反转字符串 |
| `read/write file` | 文件读写 |
| `search` | 搜索 |
| `memory` | 记忆管理 |

### 4.5 记忆系统

**设计**:
- 简单的键值对记忆存储
- 支持持久化存储（通过文件）
- 支持保存、加载和清除记忆

### 4.6 AI 模型集成

**支持的提供商**:
- OpenAI (GPT-4o, GPT-3.5)
- Anthropic (Claude 3)
- Gemini
- Llama (本地)

---

## 5. 数据结构

### 5.1 配置文件 (config.json)

```json
{
  "model": "anthropic/claude-3-opus-20240229",
  "gateway_port": 18789,
  "workspace_dir": "~/.catclaw/workspace",
  "browser_enabled": false
}
```

---

## 6. 核心流程

### 6.1 启动流程 (见 src/main.c)

```
main()
  │
  ├─ log_init()                    # 初始化日志
  ├─ config_load()                 # 加载配置
  ├─ gateway_init()                # 初始化网关
  ├─ channels_init()               # 初始化渠道
  ├─ agent_init()                  # 初始化 Agent
  │   ├─ 注册内置工具
  ├─ plugin_system_init()          # 插件系统（可选）
  ├─ skill_system_init()           # 技能系统
  ├─ thread_pool_create()          # 线程池
  └─ gateway_start()               # 启动 WebSocket 服务器
       │
       └─ 进入命令行循环
```

### 6.2 消息处理流程

```
用户消息
   │
   ▼
Gateway 接收消息 (WebSocket / CLI / 渠道)
   │
   ▼
解析命令 / 消息
   │
   ├─ 是 / 命令？ ──是──▶ 执行命令
   │
   ▼ 否
Agent 处理消息
   │
   ├─ 需要工具调用？
   │      │
   │      └─ 是 ──▶ 执行工具 ──▶ 返回结果
   │
   ▼ 否
返回响应
   │
   ▼
发送到已连接渠道
```

### 6.3 多步骤执行流程

```
添加步骤
   │
   ▼
列出步骤
   │
   ▼
执行步骤
   │
   ├─ 可以暂停 / 恢复 / 停止
   │
   ▼
完成所有步骤
```

---

## 7. 文件结构

### 7.1 源码结构

```
catclaw/
├── src/
│   ├── main.c              # 主程序入口
│   ├── config.h/c          # 配置管理
│   ├── log.h/c             # 日志系统
│   ├── gateway.h/c         # 网关
│   ├── websocket.h/c       # WebSocket
│   ├── channels.h/c        # 渠道管理
│   ├── telegram.h/c        # Telegram 渠道
│   ├── discord.h/c         # Discord 渠道
│   ├── agent.h/c           # Agent 核心
│   ├── context.h/c         # 上下文管理
│   ├── ai_model.h/c        # AI 模型集成
│   ├── session.h/c         # 会话管理
│   ├── message.h/c         # 消息处理
│   ├── tool.h/c            # 工具系统
│   ├── memory.h/c          # 记忆系统
│   ├── queue.h/c           # 消息队列
│   ├── workspace.h/c        # 工作区管理
│   ├── plugin.h/c          # 插件系统
│   ├── skill.h/c           # 技能系统
│   ├── thread_pool.h/c     # 线程池
│   ├── cJSON.h/c           # JSON 解析
│   ├── model.h/c           # 模型定义
│   └── skill_weather.c     # 示例技能
├── Makefile                # 编译脚本
├── README.md               # 说明文档
├── DESIGN.md               # 本文档
├── TESTING.md              # 测试文档
├── SKILLS.md               # 技能文档
└── catclaw-client.html     # Web 客户端示例
```

### 7.2 数据目录结构

```
~/.catclaw/
├── config.json              # 配置文件
├── workspace/               # Agent 工作区
├── agents/
│   └── main/
│       ├── agent/
│       └── sessions/
├── plugins/                 # 插件目录
└── credentials/             # 凭证目录
```

---

## 8. 扩展指南

### 8.1 添加新工具

步骤：
1. 实现工具函数
2. 在 agent_init() 中注册

### 8.2 添加新渠道

步骤：
1. 创建 channel_xxx.h 和 channel_xxx.c
2. 实现渠道接口
3. 在 channels_init() 中注册

### 8.3 编译

**Linux**:
```bash
make
./catclaw
```

**Windows (MinGW)**:
```bash
gcc -Wall -Wextra -O2 -DNO_CURL src/*.c -o catclaw.exe -lpthread -lws2_32
catclaw.exe
```

---

## 附录

### A. 参考资料

- OpenClaw 官方仓库
- CatClaw 现有代码

### B. 术语表

| 术语 | 说明 |
|------|------|
| Gateway | 网关，中心服务器 |
| Agent | 智能代理，核心执行单元 |
| Session | 会话，一次对话上下文 |
| Tool | 工具，Agent 可调用的函数 |
| Workspace | 工作区，Agent 的文件目录 |
| JSONL | JSON Lines，逐行 JSON 格式 |

---

**版本**: 1.0.0
**最后更新**: 2026-03-04