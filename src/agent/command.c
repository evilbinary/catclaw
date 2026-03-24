/**
 * Command Handler - 统一命令处理实现
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#include "command.h"
#include "common/log.h"
#include "common/config.h"
#include "common/plugin.h"
#include "agent/agent.h"
#include "gateway/gateway.h"
#include "gateway/channels.h"
#include "tool/skill.h"

// 创建命令结果
static CommandResult* result_create(bool is_cmd, bool handled, CommandAction action, const char* response) {
    CommandResult* result = (CommandResult*)malloc(sizeof(CommandResult));
    if (!result) return NULL;
    
    result->is_command = is_cmd;
    result->handled = handled;
    result->action = action;
    result->response = response ? strdup(response) : strdup("");
    
    return result;
}

// 处理 help 命令
static char* cmd_help(void) {
    size_t size = 8192;
    char* buf = (char*)malloc(size);
    if (!buf) return NULL;
    
    snprintf(buf, size,
        "📋 可用命令:\n"
        "────────────────────────\n"
        "  /help              - 显示帮助\n"
        "  /status            - 显示状态\n"
        "  /health            - 健康检查\n"
        "  /time              - 获取当前时间\n"
        "  /exit              - 退出程序\n"
        "\n"
        "模型相关:\n"
        "  /model             - 列出可用模型\n"
        "  /model <name>      - 切换模型\n"
        "\n"
        "网关相关:\n"
        "  /gateway status    - 网关状态\n"
        "  /gateway start     - 启动网关\n"
        "  /gateway stop      - 停止网关\n"
        "\n"
        "频道相关:\n"
        "  /channel list      - 列出频道\n"
        "  /channel enable <id>   - 启用频道\n"
        "  /channel disable <id>  - 禁用频道\n"
        "  /channel connect <id>  - 连接频道\n"
        "  /channel disconnect <id> - 断开频道\n"
        "\n"
        "配置相关:\n"
        "  /config list       - 列出配置\n"
        "  /config get <key>  - 获取配置\n"
        "  /config set <key> <value> - 设置配置\n"
        "\n"
        "技能相关:\n"
        "  /skill list        - 列出技能\n"
        "  /skill load <path> - 加载技能\n"
        "  /skill unload <name> - 卸载技能\n"
        "  /skill execute <name> [params] - 执行技能\n"
        "  /skill enable <name> - 启用技能\n"
        "  /skill disable <name> - 禁用技能\n"
        "\n"
        "插件相关:\n"
        "  /plugin list       - 列出插件\n"
        "  /plugin load <path> - 加载插件\n"
        "  /plugin unload <name> - 卸载插件\n"
        "\n"
        "工具命令:\n"
        "  /search <query>    - 搜索网络\n"
        "  /weather <city>    - 获取天气\n"
        "  /shell <command>   - 执行Shell命令\n"
        "\n"
        "系统控制:\n"
        "  /system restart    - 重启系统\n"
        "  /system shutdown   - 关闭系统\n"
        "\n"
        "调试命令:\n"
        "  /loglevel <level>  - 设置日志级别\n"
        "  /debug on/off      - 开启/关闭调试模式\n"
        "\n"
        "消息:\n"
        "  /message <text>    - 发送消息给AI\n");
    
    return buf;
}

// 处理 status 命令
static char* cmd_status(void) {
    size_t size = 4096;
    char* buf = (char*)malloc(size);
    if (!buf) return NULL;

    int offset = snprintf(buf, size,
        "🦞 CatClaw 状态\n"
        "────────────────────────\n\n");

    // Agent 状态
    const char* model = agent_get_model();
    offset += snprintf(buf + offset, size - offset,
        "📱 Agent:\n"
        "  模型: %s\n",
        model ? model : "未设置");

    // 推断 provider
    if (model) {
        const char* provider = "unknown";
        if (strstr(model, "anthropic") != NULL) provider = "anthropic";
        else if (strstr(model, "openai") != NULL) provider = "openai";
        else if (strstr(model, "gemini") != NULL) provider = "gemini";
        else if (strstr(model, "llama") != NULL || strstr(model, "ollama") != NULL) provider = "ollama";
        offset += snprintf(buf + offset, size - offset, "  Provider: %s\n", provider);
    }

    // Gateway 状态
    offset += snprintf(buf + offset, size - offset,
        "\n🌐 Gateway:\n"
        "  状态: %s\n",
        gateway_is_running() ? "✓ 运行中" : "✗ 未启动");

    // Channels 状态
    char* channels_str = channels_status_string();
    if (channels_str) {
        offset += snprintf(buf + offset, size - offset, "\n📺 %s", channels_str);
        free(channels_str);
    }

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
    
    if (!args || strlen(args) == 0 || strcmp(args, "list") == 0) {
        // 列出所有模型
        int offset = snprintf(buf, size, "🤖 当前模型: %s\n",
            agent_get_model() ? agent_get_model() : "未设置");
        offset += snprintf(buf + offset, size - offset, 
            "使用 /model <name> 切换模型\n");
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
        snprintf(buf, size, "网关状态: %s",
            gateway_is_running() ? "✓ 运行中" : "✗ 未启动");
    } else if (strcmp(args, "start") == 0) {
        gateway_start();
        snprintf(buf, size, "正在启动网关...");
    } else if (strcmp(args, "stop") == 0) {
        gateway_stop();
        snprintf(buf, size, "正在停止网关...");
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
        snprintf(buf, size, "📺 频道列表:\n使用 /status 查看详细状态");
    } else if (strncmp(args, "enable ", 7) == 0) {
        const char* id = args + 7;
        ChannelInstance* ch = channel_find(id);
        if (ch) {
            channel_enable(ch);
            snprintf(buf, size, "✓ 已启用频道: %s", id);
        } else {
            snprintf(buf, size, "✗ 未找到频道: %s", id);
        }
    } else if (strncmp(args, "disable ", 8) == 0) {
        const char* id = args + 8;
        ChannelInstance* ch = channel_find(id);
        if (ch) {
            channel_disable(ch);
            snprintf(buf, size, "✓ 已禁用频道: %s", id);
        } else {
            snprintf(buf, size, "✗ 未找到频道: %s", id);
        }
    } else if (strncmp(args, "connect ", 8) == 0) {
        const char* id = args + 8;
        ChannelInstance* ch = channel_find(id);
        if (ch) {
            channel_connect(ch);
            snprintf(buf, size, "✓ 已连接频道: %s", id);
        } else {
            snprintf(buf, size, "✗ 未找到频道: %s", id);
        }
    } else if (strncmp(args, "disconnect ", 11) == 0) {
        const char* id = args + 11;
        ChannelInstance* ch = channel_find(id);
        if (ch) {
            channel_disconnect(ch);
            snprintf(buf, size, "✓ 已断开频道: %s", id);
        } else {
            snprintf(buf, size, "✗ 未找到频道: %s", id);
        }
    } else {
        snprintf(buf, size, "未知子命令: channel %s", args);
    }
    
    return buf;
}

// 处理 config 命令
static char* cmd_config(const char* args) {
    size_t size = 512;
    char* buf = (char*)malloc(size);
    if (!buf) return NULL;
    
    if (!args || strlen(args) == 0 || strcmp(args, "list") == 0) {
        config_print();
        snprintf(buf, size, "配置已打印到控制台");
    } else if (strncmp(args, "get ", 4) == 0) {
        const char* key = args + 4;
        const char* value = config_get(key);
        if (value) {
            snprintf(buf, size, "%s = %s", key, value);
        } else {
            snprintf(buf, size, "✗ 未找到配置: %s", key);
        }
    } else if (strncmp(args, "set ", 4) == 0) {
        char* params = strdup(args + 4);
        char* key = strtok(params, " ");
        char* value = strtok(NULL, "");
        if (key && value) {
            if (config_set(key, value)) {
                snprintf(buf, size, "✓ 已设置: %s = %s", key, value);
            } else {
                snprintf(buf, size, "✗ 设置失败: %s", key);
            }
        } else {
            snprintf(buf, size, "用法: /config set <key> <value>");
        }
        free(params);
    } else {
        snprintf(buf, size, "未知子命令: config %s", args);
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

// 处理 debug 命令
static char* cmd_debug(const char* args) {
    size_t size = 64;
    char* buf = (char*)malloc(size);
    if (!buf) return NULL;
    
    if (strcmp(args, "on") == 0) {
        agent_set_debug_mode(true);
        snprintf(buf, size, "✓ 调试模式已开启");
    } else if (strcmp(args, "off") == 0) {
        agent_set_debug_mode(false);
        snprintf(buf, size, "✓ 调试模式已关闭");
    } else {
        snprintf(buf, size, "用法: /debug on|off");
    }
    
    return buf;
}

// 处理 plugin 命令
static char* cmd_plugin(const char* args) {
    size_t size = 256;
    char* buf = (char*)malloc(size);
    if (!buf) return NULL;
    
    if (!args || strlen(args) == 0 || strcmp(args, "list") == 0) {
        plugin_list();
        snprintf(buf, size, "插件列表已打印到控制台");
    } else if (strncmp(args, "load ", 5) == 0) {
        const char* path = args + 5;
        if (plugin_load(path)) {
            snprintf(buf, size, "✓ 已加载插件: %s", path);
        } else {
            snprintf(buf, size, "✗ 加载失败: %s", path);
        }
    } else if (strncmp(args, "unload ", 7) == 0) {
        const char* name = args + 7;
        if (plugin_unload(name)) {
            snprintf(buf, size, "✓ 已卸载插件: %s", name);
        } else {
            snprintf(buf, size, "✗ 卸载失败: %s", name);
        }
    } else {
        snprintf(buf, size, "未知子命令: plugin %s", args);
    }
    
    return buf;
}

// 处理 search 命令
static char* cmd_search(const char* args) {
    if (!args || strlen(args) == 0) {
        return strdup("用法: /search <关键词>");
    }
    
    char* result = (char*)malloc(256);
    snprintf(result, 256, "🔍 搜索: %s\n（请使用 AI 对话进行搜索）", args);
    return result;
}

// 处理 weather 命令
static char* cmd_weather(const char* args) {
    if (!args || strlen(args) == 0) {
        return strdup("用法: /weather <城市>");
    }
    
    char* result = (char*)malloc(256);
    snprintf(result, 256, "🌤️ 天气查询: %s\n（请使用 AI 对话查询天气）", args);
    return result;
}

// 处理 shell 命令
static char* cmd_shell(const char* args) {
    if (!args || strlen(args) == 0) {
        return strdup("用法: /shell <command>");
    }
    
    // 执行 shell 命令
    FILE* fp = popen(args, "r");
    if (!fp) {
        return strdup("✗ 执行命令失败");
    }
    
    size_t size = 2048;
    char* buf = (char*)malloc(size);
    if (!buf) {
        pclose(fp);
        return strdup("内存分配失败");
    }
    
    int offset = snprintf(buf, size, "💻 执行: %s\n\n", args);
    
    char line[256];
    while (fgets(line, sizeof(line), fp) && offset < (int)(size - 256)) {
        offset += snprintf(buf + offset, size - offset, "%s", line);
    }
    
    pclose(fp);
    return buf;
}

// 处理 message 命令
static char* cmd_message(const char* args) {
    if (!args || strlen(args) == 0) {
        return strdup("用法: /message <text>");
    }
    // 返回消息内容，由调用者处理发送
    return strdup(args);
}

// 处理 skill 命令
static char* cmd_skill(const char* args) {
    size_t size = 512;
    char* buf = (char*)malloc(size);
    if (!buf) return NULL;
    
    if (!args || strlen(args) == 0 || strcmp(args, "list") == 0) {
        skill_list();
        snprintf(buf, size, "技能列表已打印到控制台");
    } else if (strncmp(args, "load ", 5) == 0) {
        const char* path = args + 5;
        if (skill_load(path)) {
            snprintf(buf, size, "✓ 已加载技能: %s", path);
        } else {
            snprintf(buf, size, "✗ 加载失败: %s", path);
        }
    } else if (strncmp(args, "unload ", 7) == 0) {
        const char* name = args + 7;
        if (skill_unload(name)) {
            snprintf(buf, size, "✓ 已卸载技能: %s", name);
        } else {
            snprintf(buf, size, "✗ 卸载失败: %s", name);
        }
    } else if (strncmp(args, "execute ", 8) == 0) {
        char* params = strdup(args + 8);
        char* name = strtok(params, " ");
        char* skill_params = strtok(NULL, "");
        if (name) {
            char* result = skill_execute_skill(name, skill_params);
            if (result) {
                snprintf(buf, size, "%s", result);
                free(result);
            } else {
                snprintf(buf, size, "✗ 执行失败: %s", name);
            }
        } else {
            snprintf(buf, size, "用法: /skill execute <name> [params]");
        }
        free(params);
    } else if (strncmp(args, "enable ", 7) == 0) {
        const char* name = args + 7;
        if (skill_enable(name)) {
            snprintf(buf, size, "✓ 已启用技能: %s", name);
        } else {
            snprintf(buf, size, "✗ 启用失败: %s", name);
        }
    } else if (strncmp(args, "disable ", 8) == 0) {
        const char* name = args + 8;
        if (skill_disable(name)) {
            snprintf(buf, size, "✓ 已禁用技能: %s", name);
        } else {
            snprintf(buf, size, "✗ 禁用失败: %s", name);
        }
    } else {
        snprintf(buf, size, "未知子命令: skill %s", args);
    }
    
    return buf;
}

// 处理 system 命令
static char* cmd_system(const char* args, CommandAction* action) {
    size_t size = 256;
    char* buf = (char*)malloc(size);
    if (!buf) return NULL;
    
    if (strcmp(args, "shutdown") == 0) {
        *action = COMMAND_ACTION_SHUTDOWN;
        snprintf(buf, size, "🛑 正在关闭系统...");
    } else if (strcmp(args, "restart") == 0) {
        *action = COMMAND_ACTION_RESTART;
        snprintf(buf, size, "🔄 正在重启系统...");
    } else {
        snprintf(buf, size, "未知子命令: system %s\n可用: shutdown, restart", args);
    }
    
    return buf;
}

// 主处理函数
CommandResult* command_process(const char* input) {
    if (!input || strlen(input) == 0) {
        return result_create(false, false, COMMAND_ACTION_NONE, NULL);
    }
    
    // 检查是否是命令
    if (input[0] != '/') {
        return result_create(false, false, COMMAND_ACTION_NONE, NULL);
    }
    
    // 解析命令
    const char* cmd = input + 1;  // 跳过 /
    char* cmd_copy = strdup(cmd);
    if (!cmd_copy) {
        return result_create(true, false, COMMAND_ACTION_NONE, "内存分配失败");
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
    CommandAction action = COMMAND_ACTION_NONE;
    
    // 匹配命令
    if (strcmp(cmd_copy, "help") == 0) {
        response = cmd_help();
    } else if (strcmp(cmd_copy, "status") == 0) {
        response = cmd_status();
    } else if (strcmp(cmd_copy, "health") == 0) {
        response = cmd_health();
    } else if (strcmp(cmd_copy, "time") == 0) {
        response = cmd_time();
    } else if (strcmp(cmd_copy, "exit") == 0 || strcmp(cmd_copy, "quit") == 0) {
        response = strdup("👋 再见!");
        action = COMMAND_ACTION_EXIT;
    } else if (strcmp(cmd_copy, "message") == 0) {
        response = cmd_message(args);
        if (response && strlen(response) > 0 && strcmp(response, "用法: /message <text>") != 0) {
            action = COMMAND_ACTION_SEND_MESSAGE;
        }
    } else if (strcmp(cmd_copy, "model") == 0) {
        response = cmd_model(args);
    } else if (strcmp(cmd_copy, "gateway") == 0) {
        response = cmd_gateway(args);
    } else if (strcmp(cmd_copy, "channel") == 0) {
        response = cmd_channel(args);
    } else if (strcmp(cmd_copy, "config") == 0) {
        response = cmd_config(args);
    } else if (strcmp(cmd_copy, "loglevel") == 0) {
        response = cmd_loglevel(args);
    } else if (strcmp(cmd_copy, "debug") == 0) {
        response = cmd_debug(args);
    } else if (strcmp(cmd_copy, "plugin") == 0) {
        response = cmd_plugin(args);
    } else if (strcmp(cmd_copy, "skill") == 0 || strcmp(cmd_copy, "skills") == 0) {
        response = cmd_skill(args);
    } else if (strcmp(cmd_copy, "system") == 0) {
        response = cmd_system(args, &action);
    } else if (strcmp(cmd_copy, "search") == 0) {
        response = cmd_search(args);
    } else if (strcmp(cmd_copy, "weather") == 0) {
        response = cmd_weather(args);
    } else if (strcmp(cmd_copy, "shell") == 0) {
        response = cmd_shell(args);
    } else {
        handled = false;
        response = strdup("❌ 未知命令，输入 /help 查看帮助");
    }
    
    free(cmd_copy);
    
    CommandResult* result = result_create(true, handled, action, response);
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
