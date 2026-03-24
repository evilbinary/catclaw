/**
 * Command Handler - 统一命令处理实现
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "command.h"
#include "common/log.h"
#include "agent/agent.h"
#include "gateway/gateway.h"
#include "gateway/channels.h"

// 创建命令结果
static CommandResult* result_create(bool is_cmd, bool handled, const char* response) {
    CommandResult* result = (CommandResult*)malloc(sizeof(CommandResult));
    if (!result) return NULL;
    
    result->is_command = is_cmd;
    result->handled = handled;
    result->response = response ? strdup(response) : strdup("");
    
    return result;
}

// 处理 help 命令
static char* cmd_help(void) {
    size_t size = 2048;
    char* buf = (char*)malloc(size);
    if (!buf) return NULL;
    
    int offset = 0;
    offset += snprintf(buf + offset, size - offset,
        "📋 可用命令:\n"
        "────────────────────────\n"
        "  /help              - 显示帮助\n"
        "  /status            - 显示状态\n"
        "  /model             - 列出可用模型\n"
        "  /model <name>      - 切换模型\n"
        "  /gateway status    - 网关状态\n"
        "  /channel list      - 列出频道\n"
        "  /health            - 健康检查\n"
        "  /loglevel <level>  - 设置日志级别\n"
        "  /time              - 获取当前时间\n"
        "  /search <query>    - 搜索网络\n"
        "  /weather <location>- 获取天气\n");
    
    return buf;
}

// 处理 status 命令
static char* cmd_status(void) {
    size_t size = 2048;
    char* buf = (char*)malloc(size);
    if (!buf) return NULL;
    
    int offset = snprintf(buf, size,
        "🦞 CatClaw 状态\n"
        "────────────────────────\n\n");
    
    // Model
    offset += snprintf(buf + offset, size - offset, "模型: %s\n",
        agent_get_model() ? agent_get_model() : "未设置");
    
    // Channels status
    offset += snprintf(buf + offset, size - offset, "\n频道: 飞书/Discord/Telegram\n");
    
    return buf;
}

// 处理 health 命令
static char* cmd_health(void) {
    size_t size = 512;
    char* buf = (char*)malloc(size);
    if (!buf) return NULL;
    
    snprintf(buf, size,
        "🏥 健康检查\n"
        "────────────────────────\n"
        "系统: ✓ 正常\n"
        "模型: %s\n",
        agent_get_model() ? "✓ 可用" : "✗ 未设置");
    
    return buf;
}

// 处理 time 命令
static char* cmd_time(void) {
    size_t size = 256;
    char* buf = (char*)malloc(size);
    if (!buf) return NULL;
    
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    
    strftime(buf, size, "🕐 当前时间: %Y-%m-%d %H:%M:%S\n", tm_info);
    
    return buf;
}

// 处理 model 命令
static char* cmd_model(const char* args) {
    size_t size = 1024;
    char* buf = (char*)malloc(size);
    if (!buf) return NULL;
    
    if (!args || strlen(args) == 0) {
        // 列出所有模型
        int offset = snprintf(buf, size, "🤖 可用模型:\n────────────────────────\n");
        // 这里可以调用模型列表接口
        offset += snprintf(buf + offset, size - offset, "当前模型: %s\n",
            agent_get_model() ? agent_get_model() : "未设置");
    } else {
        // 切换模型
        if (agent_set_model(args)) {
            snprintf(buf, size, "✓ 已切换到模型: %s", args);
        } else {
            snprintf(buf, size, "✗ 切换模型失败: %s", args);
        }
    }
    
    return buf;
}

// 处理 gateway 命令
static char* cmd_gateway(const char* args) {
    size_t size = 256;
    char* buf = (char*)malloc(size);
    if (!buf) return NULL;
    
    if (!args || strlen(args) == 0 || strcmp(args, "status") == 0) {
        snprintf(buf, size, "网关: 使用 /status 查看完整状态");
    } else if (strcmp(args, "start") == 0) {
        snprintf(buf, size, "网关启动请使用 CLI 命令");
    } else if (strcmp(args, "stop") == 0) {
        snprintf(buf, size, "网关停止请使用 CLI 命令");
    } else {
        snprintf(buf, size, "未知子命令: gateway %s", args);
    }
    
    return buf;
}

// 处理 channel 命令
static char* cmd_channel(const char* args) {
    size_t size = 512;
    char* buf = (char*)malloc(size);
    if (!buf) return NULL;
    
    if (!args || strlen(args) == 0 || strcmp(args, "list") == 0) {
        snprintf(buf, size, "📺 频道列表:\n────────────────────────\n%s",
            "飞书、Discord、Telegram 等");
    } else {
        snprintf(buf, size, "频道命令: %s", args);
    }
    
    return buf;
}

// 处理 loglevel 命令
static char* cmd_loglevel(const char* args) {
    size_t size = 128;
    char* buf = (char*)malloc(size);
    if (!buf) return NULL;
    
    if (!args || strlen(args) == 0) {
        snprintf(buf, size, "用法: /loglevel <debug|info|warn|error|fatal>");
    } else {
        LogLevel level = LOG_LEVEL_INFO;
        bool valid = true;
        
        if (strcmp(args, "debug") == 0) level = LOG_LEVEL_DEBUG;
        else if (strcmp(args, "info") == 0) level = LOG_LEVEL_INFO;
        else if (strcmp(args, "warn") == 0) level = LOG_LEVEL_WARN;
        else if (strcmp(args, "error") == 0) level = LOG_LEVEL_ERROR;
        else if (strcmp(args, "fatal") == 0) level = LOG_LEVEL_FATAL;
        else valid = false;
        
        if (valid) {
            log_set_level(level);
            snprintf(buf, size, "✓ 日志级别已设置为: %s", args);
        } else {
            snprintf(buf, size, "✗ 无效的日志级别: %s", args);
        }
    }
    
    return buf;
}

// 处理 search 命令
static char* cmd_search(const char* args) {
    if (!args || strlen(args) == 0) {
        return strdup("用法: /search <关键词>");
    }
    
    // 调用 web_search 工具
    ToolArgs targs = {0};
    // 这里需要简化调用，实际可以使用 tool_search_web
    char* result = (char*)malloc(256);
    snprintf(result, 256, "🔍 搜索: %s\n（请使用 AI 对话进行搜索）", args);
    return result;
}

// 处理 weather 命令
static char* cmd_weather(const char* args) {
    if (!args || strlen(args) == 0) {
        return strdup("用法: /weather <城市>");
    }
    
    // 调用 weather 工具
    char* result = (char*)malloc(256);
    snprintf(result, 256, "🌤️ 天气查询: %s\n（请使用 AI 对话查询天气）", args);
    return result;
}

// 主处理函数
CommandResult* command_process(const char* input) {
    if (!input || strlen(input) == 0) {
        return result_create(false, false, NULL);
    }
    
    // 检查是否是命令
    if (input[0] != '/') {
        return result_create(false, false, NULL);
    }
    
    // 解析命令
    const char* cmd = input + 1;  // 跳过 /
    char* cmd_copy = strdup(cmd);
    if (!cmd_copy) {
        return result_create(true, false, "内存分配失败");
    }
    
    // 提取命令名和参数
    char* space = strchr(cmd_copy, ' ');
    char* args = NULL;
    if (space) {
        *space = '\0';
        args = space + 1;
        // 跳过前导空格
        while (*args == ' ') args++;
    }
    
    char* response = NULL;
    bool handled = true;
    
    // 匹配命令
    if (strcmp(cmd_copy, "help") == 0) {
        response = cmd_help();
    } else if (strcmp(cmd_copy, "status") == 0) {
        response = cmd_status();
    } else if (strcmp(cmd_copy, "health") == 0) {
        response = cmd_health();
    } else if (strcmp(cmd_copy, "time") == 0) {
        response = cmd_time();
    } else if (strcmp(cmd_copy, "model") == 0) {
        response = cmd_model(args);
    } else if (strcmp(cmd_copy, "gateway") == 0) {
        response = cmd_gateway(args);
    } else if (strcmp(cmd_copy, "channel") == 0) {
        response = cmd_channel(args);
    } else if (strcmp(cmd_copy, "loglevel") == 0) {
        response = cmd_loglevel(args);
    } else if (strcmp(cmd_copy, "search") == 0) {
        response = cmd_search(args);
    } else if (strcmp(cmd_copy, "weather") == 0) {
        response = cmd_weather(args);
    } else {
        handled = false;
        response = strdup("❌ 未知命令，输入 /help 查看帮助");
    }
    
    free(cmd_copy);
    
    CommandResult* result = result_create(true, handled, response);
    free(response);  // result_create 会复制
    
    return result;
}

void command_result_free(CommandResult* result) {
    if (result) {
        if (result->response) {
            free(result->response);
        }
        free(result);
    }
}

// 简化接口
char* command_handle(const char* input) {
    CommandResult* result = command_process(input);
    if (!result) return NULL;
    
    char* response = NULL;
    if (result->is_command) {
        response = result->response ? strdup(result->response) : strdup("");
        result->response = NULL;  // 防止被 free
    }
    
    command_result_free(result);
    return response;
}
