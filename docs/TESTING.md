# CatClaw 测试指南

本指南描述如何测试CatClaw的各种功能，确保系统正常运行。

## 测试环境

### 硬件要求
- 至少1GB内存
- 100MB可用磁盘空间
- 网络连接（用于AI模型和渠道连接）

### 软件要求
- C编译器（gcc或MSVC）
- 操作系统：Linux或Windows
- 必要的库：pthread（Linux）、ws2_32（Windows）

## 测试步骤

### 1. 编译测试

#### Linux
```bash
make
./catclaw
```

#### Windows
```bash
gcc -Wall -Wextra -O2 -DNO_CURL src/*.c -o catclaw.exe -lpthread -lws2_32
catclaw.exe
```

### 2. 基本功能测试

#### 帮助命令
```
catclaw> help
# 应显示所有可用命令
```

#### 状态命令
```
catclaw> status
# 应显示系统状态，包括Agent状态、模型、工具数量等
```

### 3. 工具测试

#### 计算器
```
catclaw> calculate 2 + 3
# 应返回 "Calculator result: 5"

catclaw> calc 10 * 5
# 应返回 "Calculator result: 50"
```

#### 时间查询
```
catclaw> time
# 应返回当前时间，格式为 "Current time: YYYY-MM-DD HH:MM:SS"

catclaw> now
# 应返回当前时间，格式为 "Current time: YYYY-MM-DD HH:MM:SS"
```

#### 字符串反转
```
catclaw> reverse string hello
# 应返回 "Reversed string: olleh"
```

#### 文件读写
```
catclaw> write file test.txt Hello, World!
# 应返回 "Write result: File written successfully"

catclaw> read file test.txt
# 应返回 "File content: Hello, World!"
```

#### 搜索
```
catclaw> search catclaw
# 应返回模拟的搜索结果

catclaw> web search openclaw
# 应返回模拟的搜索结果
```

#### 记忆系统
```
catclaw> memory save name John
# 应返回 "Memory saved: name = John"

catclaw> memory load name
# 应返回 "Memory load: name = John"

catclaw> memory clear
# 应返回 "Memory cleared"
```

### 4. 多步骤执行测试

```
catclaw> step add "获取当前时间"|time|
# 应返回 "Step added: 获取当前时间"

catclaw> step add "计算10+5"|calculator|10+5
# 应返回 "Step added: 计算10+5"

catclaw> step add "反转字符串"|reverse_string|hello
# 应返回 "Step added: 反转字符串"

catclaw> steps list
# 应显示所有步骤及其状态

catclaw> steps execute
# 应按顺序执行所有步骤，并显示每个步骤的结果

catclaw> steps clear
# 应清除所有步骤
```

### 5. 配置管理测试

```
catclaw> config list
# 应显示当前配置

catclaw> config set model openai/gpt-4o
# 应返回 "Config set: model = openai/gpt-4o"

catclaw> config get model
# 应返回 "Config model: openai/gpt-4o"
```

### 6. 渠道管理测试

```
catclaw> channel enable Telegram
# 应返回 "Channel Telegram enabled"

catclaw> channel connect Telegram
# 应返回 "Connecting to Telegram channel"

catclaw> channel disconnect Telegram
# 应返回 "Disconnecting from Telegram channel"

catclaw> channel disable Telegram
# 应返回 "Channel Telegram disabled"
```

### 7. 模型管理测试

```
catclaw> model list
# 应显示所有可用的AI模型

catclaw> model set anthropic/claude-3-opus-20240229
# 应返回 "Model set to: anthropic/claude-3-opus-20240229"
```

### 8. 系统管理测试

```
catclaw> system restart
# 应返回 "Restarting CatClaw..."

catclaw> system shutdown
# 应关闭CatClaw程序
```

### 9. 错误处理测试

```
catclaw> calculate invalid
# 应返回错误信息

catclaw> read file non_existent.txt
# 应返回错误信息

catclaw> channel enable InvalidChannel
# 应返回错误信息
```

### 10. 调试模式测试

```
catclaw> debug on
# 应返回 "Debug mode enabled"

# 执行一些命令，应看到详细的调试信息

catclaw> debug off
# 应返回 "Debug mode disabled"
```

## 性能测试

### 1. 多线程测试
- 同时执行多个工具调用
- 测试线程池的并发处理能力

### 2. 内存测试
- 执行大量的记忆操作
- 测试内存使用情况

### 3. 响应时间测试
- 测量工具执行的响应时间
- 测试系统在高负载下的表现

## 回归测试

每次修改代码后，应执行以下测试：

1. 编译测试
2. 基本功能测试
3. 核心工具测试
4. 多步骤执行测试
5. 错误处理测试

## 测试结果

记录测试结果，包括：
- 测试日期和时间
- 测试环境
- 测试步骤和结果
- 发现的问题和解决方案

## 故障排除

### 常见问题

1. **编译错误**
   - 缺少库：安装相应的库
   - 语法错误：检查代码语法

2. **运行错误**
   - 端口被占用：修改配置文件中的端口
   - 权限问题：确保有足够的权限

3. **功能错误**
   - 工具执行失败：检查工具参数
   - 渠道连接失败：检查网络连接和配置

### 日志查看

查看系统日志，了解详细的错误信息：
- 控制台输出
- 调试模式下的详细日志

## 测试覆盖率

确保测试覆盖以下方面：
- 所有命令和工具
- 错误处理路径
- 边界情况
- 并发场景

## 结论

通过全面的测试，确保CatClaw系统的稳定性和可靠性，为用户提供良好的使用体验。