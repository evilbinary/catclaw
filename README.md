# CatClaw - C语言版本

CatClaw是一个轻量级的C语言实现的Agent系统，基于OpenClaw功能，提供多步骤执行、状态管理、记忆系统等核心功能。

## 功能特性

### 轻量级
- 纯C实现，无外部依赖
- 模块化设计，易于扩展
- 跨平台兼容（Windows和Linux）

### 核心功能

#### 多步骤执行
- 支持"先做A，然后做B"的流程
- 支持暂停、恢复、停止执行
- 步骤管理和状态跟踪

#### 状态管理
- 完整的Agent状态跟踪
- 状态：Idle（空闲）、Executing（执行中）、Paused（暂停）、Error（错误）
- 详细的状态信息显示

#### 记忆系统
- 简单的键值对记忆存储
- 支持保存、加载和清除记忆
- 持久化存储（通过文件）

#### 错误处理
- 详细的错误信息存储和显示
- 错误状态自动设置
- 调试模式支持

#### 内置工具
- **计算器**：执行基本算术运算
- **时间查询**：获取当前时间
- **字符串操作**：反转字符串
- **文件读写**：读取和写入文件
- **模拟网络搜索**：模拟搜索结果
- **记忆保存/读取**：管理记忆

### 命令行工具
- **配置管理**：查看和修改配置
- **渠道管理**：启用、禁用、连接和断开渠道
- **模型管理**：查看和设置AI模型
- **系统管理**：重启和关闭系统

## 安装方法

### Linux
1. 确保安装了gcc编译器
2. 克隆项目：`git clone https://github.com/evilbinary/catclaw.git`
3. 进入项目目录：`cd catclaw`
4. 编译项目：`make`
5. 运行：`./catclaw`

### Windows
1. 安装MSYS2或MinGW
2. 克隆项目：`git clone https://github.com/evilbinary/catclaw.git`
3. 进入项目目录：`cd catclaw`
4. 使用MSYS2的gcc编译：`gcc -Wall -Wextra -O2 -DNO_CURL src/*.c -o catclaw.exe -lpthread -lws2_32`
5. 运行：`catclaw.exe`

## 使用说明

### 基本命令
- `help`：显示帮助信息
- `status`：显示系统状态
- `exit`：退出程序

### 工具使用
- **计算器**：`calculate 2+3` 或 `calc 10*5`
- **时间查询**：`time` 或 `now`
- **字符串反转**：`reverse string hello`
- **文件读写**：`read file filename.txt`、`write file filename.txt content`
- **搜索**：`search keyword` 或 `web search keyword`
- **记忆**：`memory save key value`、`memory load key`、`memory clear`

### 多步骤执行
1. 添加步骤：`step add "获取时间"|time|`
2. 列出步骤：`steps list`
3. 执行步骤：`steps execute`
4. 暂停执行：`steps pause`
5. 恢复执行：`steps resume`
6. 停止执行：`steps stop`
7. 清除步骤：`steps clear`

### 配置管理
- 查看配置：`config list`
- 设置配置：`config set <key> <value>`
- 获取配置：`config get <key>`

### 渠道管理
- 启用渠道：`channel enable <name>`
- 禁用渠道：`channel disable <name>`
- 连接渠道：`channel connect <name>`
- 断开渠道：`channel disconnect <name>`

### 模型管理
- 查看模型：`model list`
- 设置模型：`model set <model>`

### 系统管理
- 重启系统：`system restart`
- 关闭系统：`system shutdown`

## 架构设计

### 模块结构
- **config**：配置管理
- **gateway**：网关服务器（WebSocket）
- **channels**：渠道管理（Telegram、Discord等）
- **agent**：核心代理，处理命令和执行工具
- **ai_model**：AI模型集成
- **plugin**：插件系统
- **thread_pool**：线程池，处理并发
- **log**：日志系统
- **websocket**：WebSocket实现

### 架构图

```
+----------------------+
|      命令行界面       |
+----------------------+
          |
          v
+----------------------+
|      命令解析器       |
+----------------------+
          |
          v
+----------------------+
|        Agent         |<------------------------+
+----------------------+                         |
|                      |                         |
| +------------------+ |     +----------------+  |
| |    状态管理      | |<----|    多步骤执行  |  |
| +------------------+ |     +----------------+  |
|                      |                         |
| +------------------+ |     +----------------+  |
| |    记忆系统      | |<----|     工具库     |  |
| +------------------+ |     +----------------+  |
|                      |                         |
| +------------------+ |     +----------------+  |
| |    错误处理      | |<----|    AI模型     |  |
| +------------------+ |     +----------------+  |
+----------------------+
          |
          v
+----------------------+
|      渠道管理         |
+----------------------+
          |
          v
+----------------------+
|  外部渠道（Telegram、 |
|  Discord、WebChat）   |
+----------------------+
```

### 数据流
1. 用户输入命令
2. 命令解析器解析命令
3. Agent执行相应的工具或操作
4. Agent管理执行状态和记忆
5. 返回结果给用户
6. 同时发送到已连接的渠道

## 示例用法

### 示例1：使用计算器
```
catclaw> calculate 10 + 5 * 2
Calculator result: 20
```

### 示例2：多步骤执行
```
catclaw> step add "获取当前时间"|time|
Step added: 获取当前时间
catclaw> step add "计算10+5"|calculator|10+5
Step added: 计算10+5
catclaw> step add "反转字符串"|reverse_string|hello
Step added: 反转字符串
catclaw> steps list
Defined steps:
  Step 1: 获取当前时间
    Tool: time
    Params:
    Status: Pending
  Step 2: 计算10+5
    Tool: calculator
    Params: 10+5
    Status: Pending
  Step 3: 反转字符串
    Tool: reverse_string
    Params: hello
    Status: Pending
catclaw> steps execute
Starting execution of 3 steps
Executing step 1: 获取当前时间
Step 1 result: 2026-02-28 14:30:00
Executing step 2: 计算10+5
Step 2 result: 15
Executing step 3: 反转字符串
Step 3 result: olleh
All steps completed
```

### 示例3：使用记忆系统
```
catclaw> memory save name John
Memory saved: name = John
catclaw> memory load name
Memory load: name = John
```

## 配置文件

配置文件位于 `~/.catclaw/config.json`，格式如下：

```json
{
  "model": "anthropic/claude-3-opus-20240229",
  "gateway_port": 18789,
  "workspace_dir": "~/.catclaw/workspace",
  "browser_enabled": false
}
```

## 插件系统

CatClaw支持动态加载插件，插件类型包括：
- 渠道插件：添加新的消息渠道
- 工具插件：添加新的工具
- 技能插件：添加新的技能

插件文件应放在 `~/.catclaw/plugins` 目录中。

## 调试模式

启用调试模式可以查看详细的执行过程：

```
catclaw> debug on
Debug mode enabled
```

## 常见问题

### 编译错误
- **缺少curl库**：使用 `-DNO_CURL` 编译选项
- **Windows下编译**：需要链接 `ws2_32.lib` 库

### 运行错误
- **端口被占用**：修改配置文件中的 `gateway_port`
- **AI模型错误**：检查API密钥和网络连接

## 贡献

欢迎贡献代码和提出建议！请通过GitHub提交Pull Request。

## 许可证

Apache License Version 2.0

## 联系方式

- GitHub: https://github.com/evilbinary/catclaw
- 邮箱: rootntsd@gmail.com
