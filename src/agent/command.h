/**
 * Command Handler - 统一命令处理
 * 支持从 CLI、飞书、HTTP API 等各渠道调用
 */
#ifndef COMMAND_H
#define COMMAND_H

#include <stdbool.h>

// 命令动作类型（CLI 特有动作）
typedef enum {
    COMMAND_ACTION_NONE,        // 无特殊动作
    COMMAND_ACTION_EXIT,        // 退出程序
    COMMAND_ACTION_RESTART,     // 重启系统
    COMMAND_ACTION_SHUTDOWN,    // 关闭系统
    COMMAND_ACTION_SEND_MESSAGE // 发送消息给 AI (response 中存储消息内容)
} CommandAction;

// 命令处理结果
typedef struct {
    bool is_command;       // 是否是命令（以 / 开头）
    bool handled;          // 是否已处理
    CommandAction action;  // 需要执行的额外动作
    char* response;        // 响应内容（需要调用 command_result_free 释放）
} CommandResult;

// 命令上下文
typedef struct {
    const char* chat_id;   // 当前会话ID
    const char* sender_id; // 发送者ID
    int channel_type;      // 渠道类型
} CommandContext;

// 处理输入，如果是命令则执行并返回结果
CommandResult* command_process(const char* input);

// 带上下文的命令处理
CommandResult* command_process_with_context(const char* input, const CommandContext* ctx);

// 释放命令结果
void command_result_free(CommandResult* result);

// 直接处理命令并返回响应字符串（简化接口，需要 free）
// 如果不是命令返回 NULL，如果是命令返回响应（可能是空字符串）
// 注意：此接口不返回 action，仅用于简单场景
char* command_handle(const char* input);

#endif // COMMAND_H
