# CatClaw (C 语言版) 详细设计文档

## 一、项目概述

### 1.1 项目目标
创建一个轻量级的、用 C 语言实现的 OpenClaw 核心功能子集，保留 OpenClaw 的设计理念，但大幅简化实现复杂度。

### 1.2 设计原则
- **极简主义**：只实现最核心的功能
- **跨平台**：支持 Linux/macOS/Windows
- **高性能**：C 语言实现，资源占用低
- **可扩展**：模块化设计，便于后续扩展
- **文件优先**：使用普通文件存储，便于调试和迁移

---

## 二、核心功能模块

### 2.1 模块架构图

```
┌─────────────────────────────────────────────────────────────┐
│                        CLI / API 层                           │
├─────────────────────────────────────────────────────────────┤
│                   消息处理模块 (message.c)                    │
├───────────────┬───────────────┬───────────────┬──────────────┤
│ 会话管理模块  │ 智能体循环    │ 工具执行模块  │ 记忆管理模块 │
│ (session.c)   │ (agent.c)     │ (tool.c)      │ (memory.c)   │
├───────────────┼───────────────┼───────────────┼──────────────┤
│  配置模块     │  队列模块     │  模型连接     │  文件 I/O    │
│  (config.c)   │  (queue.c)    │  (model.c)    │  (file.c)    │
└───────────────┴───────────────┴───────────────┴──────────────┘
```

---

## 三、数据结构设计

### 3.1 核心数据结构

#### 3.1.1 配置结构体 (config.h)
```c
typedef struct {
    char* workspace_path;      // 工作区路径
    char* model_provider;      // 模型提供商 (openai)
    char* model_name;          // 模型名称 (gpt-3.5-turbo)
    char* api_key;             // API 密钥
    char* api_base_url;        // API 基础 URL
    int max_context_tokens;    // 最大上下文 token 数
    int timeout_seconds;       // 超时时间（秒）
    int enable_compaction;     // 是否启用压缩
    int compaction_threshold;  // 压缩阈值（token 数）
} Config;
```

#### 3.1.2 消息结构体 (message.h)
```c
typedef enum {
    ROLE_USER,
    ROLE_ASSISTANT,
    ROLE_SYSTEM,
    ROLE_TOOL
} MessageRole;

typedef struct {
    MessageRole role;
    char* content;
    char* tool_name;       // 工具名称（如果是工具消息）
    char* tool_call_id;    // 工具调用 ID
    long long timestamp;   // 时间戳
} Message;

typedef struct {
    Message* messages;
    int count;
    int capacity;
} MessageList;
```

#### 3.1.3 会话结构体 (session.h)
```c
typedef struct {
    char* session_id;       // 会话 ID
    char* session_key;      // 会话键
    MessageList* history;   // 消息历史
    long long created_at;   // 创建时间
    long long updated_at;   // 更新时间
    int compaction_count;   // 压缩次数
    char* metadata;         // JSON 格式的元数据
} Session;
```

#### 3.1.4 队列结构体 (queue.h)
```c
typedef enum {
    QUEUE_MODE_COLLECT,     // 收集模式（默认）
    QUEUE_MODE_STEER,       // 引导模式
    QUEUE_MODE_FOLLOWUP     // 后续模式
} QueueMode;

typedef struct {
    char* session_key;
    Message* message;
    QueueMode mode;
    long long enqueue_time;
} QueueItem;

typedef struct {
    QueueItem* items;
    int front;
    int rear;
    int count;
    int capacity;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} MessageQueue;
```

#### 3.1.5 工具结构体 (tool.h)
```c
typedef struct {
    char* name;
    char* description;
    char* parameters_schema;  // JSON Schema
    int (*execute)(const char* args, char** result, int* result_len);
} Tool;

typedef struct {
    Tool* tools;
    int count;
    int capacity;
} ToolRegistry;
```

---

## 四、核心流程设计

### 4.1 系统启动流程

```
启动程序
    ↓
加载配置文件 (config_load())
    ↓
初始化工作区 (workspace_init())
    ↓
初始化工具注册表 (tool_init())
    ↓
初始化会话管理器 (session_manager_init())
    ↓
初始化消息队列 (queue_init())
    ↓
启动工作线程 (worker_thread_start())
    ↓
进入主事件循环
```

**伪代码实现：**
```c
int main(int argc, char* argv[]) {
    // 1. 加载配置
    Config* config = config_load("~/.catclaw/config.json");
    if (!config) {
        fprintf(stderr, "Failed to load config\n");
        return 1;
    }

    // 2. 初始化工作区
    if (workspace_init(config->workspace_path) != 0) {
        fprintf(stderr, "Failed to init workspace\n");
        return 1;
    }

    // 3. 初始化工具
    ToolRegistry* tools = tool_init();
    register_builtin_tools(tools);

    // 4. 初始化会话管理器
    SessionManager* session_mgr = session_manager_init(config);

    // 5. 初始化队列
    MessageQueue* queue = queue_init(100);

    // 6. 启动工作线程
    WorkerContext* worker_ctx = worker_create(config, tools, session_mgr, queue);
    worker_start(worker_ctx);

    // 7. CLI 交互循环
    cli_main_loop(worker_ctx);

    // 8. 清理
    worker_stop(worker_ctx);
    queue_destroy(queue);
    session_manager_destroy(session_mgr);
    tool_destroy(tools);
    config_destroy(config);

    return 0;
}
```

### 4.2 消息处理完整流程

```
接收用户消息
    ↓
解析消息内容
    ↓
确定会话键
    ↓
获取或创建会话 (session_get_or_create())
    ↓
将消息入队 (queue_enqueue())
    ↓
工作线程处理队列项
    ↓
    ├─→ 组装系统提示 (build_system_prompt())
    ├─→ 加载会话历史 (session_load_history())
    ├─→ 加载记忆文件 (memory_load())
    ├─→ 构建完整上下文
    ↓
检查上下文大小
    ├─→ 超过阈值 → 执行压缩 (compaction_run())
    └─→ 未超过 → 继续
    ↓
调用模型 API (model_call())
    ↓
接收模型响应
    ↓
解析工具调用 (parse_tool_calls())
    ↓
    ├─→ 有工具调用 → 执行工具 (tool_execute())
    │       ↓
    │   将工具结果加入消息列表
    │       ↓
    │   再次调用模型（循环）
    └─→ 无工具调用 → 继续
    ↓
保存会话历史 (session_save_history())
    ↓
发送响应给用户
```

**伪代码实现（工作线程）：**
```c
void* worker_thread(void* arg) {
    WorkerContext* ctx = (WorkerContext*)arg;

    while (!ctx->should_stop) {
        // 1. 从队列获取消息
        QueueItem* item = queue_dequeue(ctx->queue, 1000);
        if (!item) continue;

        // 2. 获取会话
        Session* session = session_manager_get_or_create(
            ctx->session_mgr, item->session_key);

        // 3. 添加用户消息到会话
        session_add_message(session, item->message);

        // 4. 构建上下文
        MessageList* context = build_context(ctx->config, session);

        // 5. 检查并执行压缩
        if (should_compact(ctx->config, context)) {
            compact_session(ctx->config, session);
            // 重新构建上下文
            context = build_context(ctx->config, session);
        }

        // 6. 智能体循环
        int max_iterations = 10;
        for (int i = 0; i < max_iterations; i++) {
            // 调用模型
            ModelResponse* response = model_call(ctx->config, context);
            if (!response) break;

            // 添加助手消息
            Message* assistant_msg = message_create(ROLE_ASSISTANT, response->content);
            session_add_message(session, assistant_msg);

            // 检查是否有工具调用
            ToolCall* tool_calls = parse_tool_calls(response);
            if (!tool_calls || tool_calls->count == 0) {
                // 没有工具调用，结束循环
                break;
            }

            // 执行工具调用
            for (int j = 0; j < tool_calls->count; j++) {
                ToolCall* call = &tool_calls[j];
                char* result = NULL;
                int result_len = 0;

                if (tool_execute(ctx->tools, call->name, call->arguments,
                                &result, &result_len) == 0) {
                    Message* tool_msg = message_create_tool(
                        call->id, call->name, result);
                    session_add_message(session, tool_msg);
                    free(result);
                }
            }

            // 更新上下文，继续循环
            context = build_context(ctx->config, session);
        }

        // 7. 保存会话
        session_save(session);

        // 8. 输出响应
        printf("Assistant: %s\n", assistant_msg->content);

        // 清理
        queue_item_free(item);
    }

    return NULL;
}
```

### 4.3 会话管理流程

#### 4.3.1 会话创建/加载流程
```
输入：session_key
    ↓
检查会话文件是否存在 (session_file_exists())
    ├─→ 存在 → 读取会话文件 (session_load_from_file())
    └─→ 不存在 → 创建新会话 (session_create())
    ↓
返回 Session 对象
```

#### 4.3.2 会话保存流程
```
输入：Session 对象
    ↓
序列化消息历史为 JSONL (messages_to_jsonl())
    ↓
写入会话历史文件 (<session_id>.jsonl)
    ↓
更新会话元数据 (updated_at 等)
    ↓
序列化元数据为 JSON
    ↓
写入 sessions.json
```

**会话文件格式：**
```
~/.catclaw/
├── config.json
├── workspace/
│   ├── AGENTS.md
│   ├── SOUL.md
│   ├── MEMORY.md
│   └── memory/
│       └── 2026-03-04.md
└── agents/
    └── main/
        └── sessions/
            ├── sessions.json
            ├── agent-main-main.jsonl
            └── agent-main-group-123.jsonl
```

**sessions.json 格式：**
```json
{
  "agent:main:main": {
    "sessionId": "agent-main-main",
    "sessionKey": "agent:main:main",
    "createdAt": 1709500000,
    "updatedAt": 1709510000,
    "compactionCount": 2
  }
}
```

**会话历史 JSONL 格式：**
```jsonl
{"role":"user","content":"你好","timestamp":1709500000}
{"role":"assistant","content":"你好！有什么可以帮助你的？","timestamp":1709500001}
{"role":"user","content":"今天天气怎么样？","timestamp":1709510000}
```

### 4.4 压缩流程

```
输入：Session 对象
    ↓
确定压缩分割点 (find_compaction_split_point())
    ├─→ 保留最近 N 条消息
    └─→ 压缩更早的消息
    ↓
构建压缩请求 (build_compaction_prompt())
    ├─→ 系统提示："请将以下对话总结为一条简洁的摘要"
    └─→ 上下文：需要压缩的消息
    ↓
调用模型获取摘要
    ↓
创建摘要消息 (ROLE_SYSTEM)
    ↓
重建会话历史
    ├─→ [摘要消息] + [最近 N 条消息]
    ↓
保存会话
    ↓
更新压缩计数
```

**伪代码实现：**
```c
int compaction_run(Config* config, Session* session) {
    // 1. 确定分割点
    int split_point = find_split_point(session->history, config->compaction_threshold);
    if (split_point <= 1) return 0;  // 不需要压缩

    // 2. 提取要压缩的消息
    MessageList* to_compact = message_list_slice(session->history, 0, split_point);

    // 3. 构建压缩提示
    MessageList* compaction_context = message_list_create();
    Message* sys_msg = message_create(ROLE_SYSTEM,
        "请将以下对话历史总结为一条简洁的摘要。"
        "保留关键决策、重要信息和未解决的问题。");
    message_list_append(compaction_context, sys_msg);

    for (int i = 0; i < to_compact->count; i++) {
        message_list_append(compaction_context, to_compact->messages[i]);
    }

    // 4. 调用模型获取摘要
    ModelResponse* response = model_call(config, compaction_context);
    if (!response) {
        message_list_destroy(compaction_context);
        message_list_destroy(to_compact);
        return -1;
    }

    // 5. 创建摘要消息
    char* summary_content = malloc(strlen(response->content) + 100);
    sprintf(summary_content, "[压缩摘要] %s", response->content);
    Message* summary_msg = message_create(ROLE_SYSTEM, summary_content);
    free(summary_content);

    // 6. 重建会话历史
    MessageList* new_history = message_list_create();
    message_list_append(new_history, summary_msg);

    for (int i = split_point; i < session->history->count; i++) {
        message_list_append(new_history, session->history->messages[i]);
    }

    // 7. 替换历史
    message_list_destroy(session->history);
    session->history = new_history;
    session->compaction_count++;

    // 8. 保存
    session_save(session);

    // 清理
    message_list_destroy(compaction_context);
    message_list_destroy(to_compact);
    model_response_free(response);

    return 1;
}
```

### 4.5 工具执行流程

```
输入：工具名称、工具参数
    ↓
在工具注册表中查找工具 (tool_registry_find())
    ├─→ 未找到 → 返回错误
    └─→ 找到 → 继续
    ↓
验证工具参数 (tool_validate_params())
    ├─→ 验证失败 → 返回错误
    └─→ 验证成功 → 继续
    ↓
执行工具函数 (tool->execute())
    ↓
捕获执行结果和错误
    ↓
返回结果
```

**内置工具定义：**
```c
// 工具注册表初始化
ToolRegistry* tool_init(void) {
    ToolRegistry* registry = malloc(sizeof(ToolRegistry));
    registry->capacity = 10;
    registry->count = 0;
    registry->tools = malloc(sizeof(Tool) * registry->capacity);

    // 注册内置工具
    register_tool(registry, tool_read_create());
    register_tool(registry, tool_write_create());
    register_tool(registry, tool_exec_create());
    register_tool(registry, tool_memory_search_create());

    return registry;
}

// read 工具实现
int tool_read_execute(const char* args_json, char** result, int* result_len) {
    // 解析参数
    cJSON* args = cJSON_Parse(args_json);
    const char* path = cJSON_GetStringValue(cJSON_GetObjectItem(args, "path"));

    // 读取文件
    char* content = file_read_all(path);
    if (!content) {
        asprintf(result, "Error: Cannot read file %s", path);
        *result_len = strlen(*result);
        cJSON_Delete(args);
        return -1;
    }

    // 返回结果
    *result = content;
    *result_len = strlen(content);
    cJSON_Delete(args);
    return 0;
}

Tool* tool_read_create(void) {
    Tool* tool = malloc(sizeof(Tool));
    tool->name = strdup("read");
    tool->description = strdup("读取文件内容");
    tool->parameters_schema = strdup(
        "{\"type\":\"object\",\"properties\":{"
        "\"path\":{\"type\":\"string\",\"description\":\"文件路径\"}"
        "},\"required\":[\"path\"]}");
    tool->execute = tool_read_execute;
    return tool;
}
```

### 4.6 模型调用流程

```
输入：MessageList（上下文）
    ↓
构建 API 请求 (build_api_request())
    ├─→ 序列化消息为 JSON
    ├─→ 设置模型参数（temperature, max_tokens 等）
    └─→ 添加 API 密钥认证头
    ↓
发送 HTTP POST 请求 (http_post())
    ↓
接收 HTTP 响应
    ↓
解析响应 JSON (parse_response())
    ├─→ 检查错误
    ├─→ 提取助手消息
    └─→ 提取工具调用
    ↓
返回 ModelResponse
```

**HTTP 请求格式（OpenAI 兼容）：**
```http
POST https://api.openai.com/v1/chat/completions
Authorization: Bearer YOUR_API_KEY
Content-Type: application/json

{
  "model": "gpt-3.5-turbo",
  "messages": [
    {"role": "system", "content": "你是一个有用的助手"},
    {"role": "user", "content": "你好"}
  ],
  "temperature": 0.7,
  "max_tokens": 1000
}
```

**响应格式：**
```json
{
  "id": "chatcmpl-123",
  "object": "chat.completion",
  "choices": [{
    "index": 0,
    "message": {
      "role": "assistant",
      "content": "你好！有什么可以帮助你的？",
      "tool_calls": [
        {
          "id": "call_123",
          "type": "function",
          "function": {
            "name": "read",
            "arguments": "{\"path\":\"test.txt\"}"
          }
        }
      ]
    },
    "finish_reason": "tool_calls"
  }],
  "usage": {
    "prompt_tokens": 50,
    "completion_tokens": 100,
    "total_tokens": 150
  }
}
```

---

## 五、目录结构设计

```
catclaw/
├── src/
│   ├── main.c              # 主入口
│   ├── config.c/h          # 配置管理
│   ├── session.c/h         # 会话管理
│   ├── message.c/h         # 消息处理
│   ├── queue.c/h           # 消息队列
│   ├── agent.c/h           # 智能体循环
│   ├── tool.c/h            # 工具执行
│   ├── model.c/h           # 模型连接
│   ├── memory.c/h          # 记忆管理
│   ├── file.c/h            # 文件 I/O 工具
│   ├── json.c/h            # JSON 处理
│   ├── http.c/h            # HTTP 客户端
│   ├── cli.c/h             # CLI 界面
│   └── util.c/h            # 工具函数
├── include/
│   └── catclaw/      # 公共头文件
├── tests/
│   ├── test_config.c
│   ├── test_session.c
│   ├── test_message.c
│   └── test_queue.c
├── examples/
│   └── workspace/           # 示例工作区
├── Makefile                 # Make 构建文件
└── README.md
```

---

## 六、依赖库选择

### 6.1 必需依赖
- **cJSON** (https://github.com/DaveGamble/cJSON) - JSON 解析和生成
  - 理由：轻量级、纯 C、无依赖、MIT 许可

- **libcurl** (https://curl.se/libcurl/) - HTTP 客户端
  - 理由：成熟、稳定、跨平台、支持 HTTPS

### 6.2 可选依赖
- **libuv** (https://libuv.org/) - 跨平台异步 I/O
  - 用于：高级队列、多线程
  - 理由：高性能、跨平台

- **sqlite3** (https://www.sqlite.org/) - 嵌入式数据库
  - 用于：会话存储替代方案、向量记忆
  - 理由：轻量级、零配置

### 6.3 构建工具
- **CMake** - 跨平台构建系统
- **pkg-config** - 库依赖管理

---

## 七、API 设计

### 7.1 公共 API (catclaw.h)

```c
#ifndef MINI_OPENCLAW_H
#define MINI_OPENCLAW_H

#include <stddef.h>

// 初始化/清理
int mini_openclaw_init(const char* config_path);
void mini_openclaw_shutdown(void);

// 会话管理
typedef void* SessionHandle;
SessionHandle session_create(const char* session_key);
SessionHandle session_get(const char* session_key);
void session_close(SessionHandle session);

// 消息发送
int send_message(SessionHandle session, const char* content);
const char* get_last_response(SessionHandle session);

// 工具注册
typedef int (*ToolFunc)(const char* args, char** result, int* result_len);
int register_tool(const char* name, const char* description,
                  const char* params_schema, ToolFunc func);

// 配置
void set_config(const char* key, const char* value);
const char* get_config(const char* key);

#endif
```

### 7.2 CLI 命令

```bash
# 启动交互模式
catclaw

# 发送单条消息
catclaw --message "你好"

# 指定会话
catclaw --session "agent:main:main"

# 指定配置文件
catclaw --config ~/.catclaw/config.json

# 列出会话
catclaw --list-sessions

# 删除会话
catclaw --delete-session "agent:main:main"
```

---

## 八、配置文件设计

### 8.1 config.json 格式

```json
{
  "workspace": "~/.catclaw/workspace",
  "model": {
    "provider": "openai",
    "name": "gpt-3.5-turbo",
    "apiKey": "sk-xxxxxxxxxx",
    "baseUrl": "https://api.openai.com/v1",
    "temperature": 0.7,
    "maxTokens": 2000,
    "timeout": 60
  },
  "session": {
    "mainKey": "main",
    "dmScope": "main",
    "reset": {
      "mode": "daily",
      "atHour": 4
    }
  },
  "compaction": {
    "enabled": true,
    "thresholdTokens": 3000,
    "reserveTokens": 1000,
    "memoryFlush": {
      "enabled": true,
      "softThresholdTokens": 500
    }
  },
  "tools": {
    "enabled": ["read", "write", "exec", "memory_search"],
    "disabled": []
  },
  "memory": {
    "searchEnabled": false
  }
}
```

---

## 九、实现优先级

### Phase 1: 核心功能（MVP）
- [x] 配置文件加载
- [x] 消息数据结构
- [x] 会话管理（文件存储）
- [x] 基础模型调用（OpenAI API）
- [x] CLI 交互界面
- [x] 简单的系统提示

### Phase 2: 增强功能
- [ ] 工具执行（read/write/exec）
- [ ] 消息队列（单线程版本）
- [ ] 会话历史持久化
- [ ] 记忆文件加载
- [ ] 错误处理和日志

### Phase 3: 高级功能
- [ ] 自动压缩
- [ ] 工具调用循环
- [ ] 多线程队列
- [ ] 记忆搜索（基础版本）
- [ ] 流式响应

### Phase 4: 优化和扩展
- [ ] 性能优化
- [ ] 更多模型提供商
- [ ] 插件系统
- [ ] HTTP API 服务
- [ ] Web 界面

---

## 十、测试计划

### 10.1 单元测试
- 配置解析测试
- 消息序列化/反序列化测试
- 会话文件 I/O 测试
- 工具执行测试（模拟）
- JSON 处理测试

### 10.2 集成测试
- 端到端对话测试
- 工具调用循环测试
- 会话持久化测试
- 错误恢复测试

### 10.3 性能测试
- 长会话性能
- 内存占用测试
- 并发会话测试

---

## 十一、安全考虑

### 11.1 API 密钥安全
- 配置文件权限：0600
- 不在日志中打印 API 密钥
- 支持环境变量：`CATCLAW_API_KEY`

### 11.2 工具执行安全
- 默认禁用 exec 工具
- exec 工具白名单机制
- 工作区路径限制（沙箱概念）

### 11.3 文件操作安全
- 路径规范化，防止路径遍历
- 工作区根目录限制
- 文件权限检查

---

## 十二、未来扩展方向

1. **向量记忆**：集成 SQLite + sqlite-vec 实现语义搜索
2. **多通道支持**：WebSocket API、简单的 Web 界面
3. **插件系统**：动态加载 .so/.dll 插件
4. **流式响应**：支持 Server-Sent Events (SSE)
5. **多语言模型**：支持更多提供商（Anthropic、Gemini 等）
6. **会话压缩**：更智能的压缩算法
7. **技能系统**：基于文本文件的技能加载

---

## 十三、参考资料

- OpenClaw 官方文档：docs/zh-CN/concepts/
- OpenAI API 文档：https://platform.openai.com/docs/api-reference
- cJSON：https://github.com/DaveGamble/cJSON
- libcurl：https://curl.se/libcurl/
