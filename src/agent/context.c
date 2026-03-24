#include "context.h"
#include "common/config.h"
#include "gateway/channels.h"
#include "model/ai_model.h"
#include "agent/agent.h"
#include "tool/tool.h"
#include "common/cJSON.h"
#include "session/session.h"
#include "session/message.h"
#include "common/queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>

// Default system prompt for the agent
static const char* DEFAULT_SYSTEM_PROMPT =
"你是一个有用的助手，可以帮助用户完成各种任务。请保持对话的连贯性，基于上下文进行回复。\n"
"\n"
"可用工具（使用关键字参数格式）：\n"
"1. (get-weather location \"城市名\") - 获取天气信息\n"
"2. (web-search query \"搜索词\") - 搜索网络信息\n"
"3. (calculator expression \"表达式\") - 计算数学表达式\n"
"4. (time) - 获取当前时间\n"
"5. (read-file path \"文件路径\") - 读取文件内容\n"
"6. (write-file path \"路径\" content \"内容\") - 写入文件内容\n"
"7. (reverse-string text \"文本\") - 反转字符串\n"
"8. (memory-save key \"键名\" value \"值\") - 保存信息到内存\n"
"9. (memory-load key \"键名\") - 从内存读取信息\n"
"10. (list-directory path \"目录路径\") - 列出目录内容，支持 ~ 展开\n"
"11. (web-fetch url \"URL地址\") - 获取网页内容\n"
"12. (shell command \"命令\") - 执行系统命令\n"
"\n"
"如果需要使用工具，请使用 S表达式格式输出：\n"
"(tool-calls\n"
"  (工具名 参数名 \"参数值\" ...)\n"
")\n"
"\n"
"示例：\n"
"用户：北京天气怎么样？\n"
"助手：(tool-calls (get-weather location \"北京\"))\n"
"\n"
"用户：列出 ~/.catclaw 目录\n"
"助手：(tool-calls (list-directory path \"~/.catclaw\"))\n"
"\n"
"用户：获取 https://example.com 的内容\n"
"助手：(tool-calls (web-fetch url \"https://example.com\"))\n"
"\n"
"工具执行后返回格式：[TOOL_RESULT] 结果 [/TOOL_RESULT]\n"
"看到 [TOOL_RESULT] 后直接回复用户，不要再调用工具！\n";
#include "common/log.h"

// ToolCall structure for tool execution
typedef struct {
    char* id;
    char* name;
    ToolArgs* args;  // Parsed arguments
} ToolCall;

// ToolCallList structure
typedef struct {
    ToolCall* calls;
    int count;
} ToolCallList;

// ==================== 流式消息系统 ====================

// 全局流式状态
static bool g_stream_active = false;

// 节流控制
static int g_last_sent_len = 0;
static struct timespec g_last_update_time = {0, 0};

#define STREAM_MIN_INTERVAL_MS  300  // 最小更新间隔（配合飞书API延迟）
#define STREAM_MIN_CHARS        50   // 最小新增字符数

// 用于遍历 channel 时的数据传递
typedef struct {
    const char* content;
} StreamCallbackData;

// 启动流式消息的遍历回调
static void stream_start_iterator(ChannelInstance* channel, void* user_data) {
    StreamCallbackData* data = (StreamCallbackData*)user_data;
    if (channel->stream_start && channel->stream_update) {
        // 通过线程池串行队列启动流式消息
        channel_stream_submit_task(channel, STREAM_TASK_START, data->content);
        log_debug("[Stream] Started stream for channel: %s", channel->name);
    }
}

// 更新流式消息的遍历回调
static void stream_update_iterator(ChannelInstance* channel, void* user_data) {
    StreamCallbackData* data = (StreamCallbackData*)user_data;
    if (channel->stream_update) {
        channel_stream_submit_task(channel, STREAM_TASK_UPDATE, data->content);
    }
}

// 结束流式消息的遍历回调
static void stream_end_iterator(ChannelInstance* channel, void* user_data) {
    (void)user_data;
    if (channel->stream_end) {
        channel_stream_submit_task(channel, STREAM_TASK_END, NULL);
    }
}

// 检查 tool-calls 是否完整（有闭合的括号）
// 返回: true=完整或无tool-calls, false=不完整（需要等待）
static bool tool_calls_is_complete(const char* content) {
    if (!content) return true;
    
    const char* tc_start = strstr(content, "(tool-calls");
    if (!tc_start) {
        // 检查是否有不完整的 "(tool" 前缀
        const char* tool_pos = strstr(content, "(tool");
        if (tool_pos) {
            const char* after = tool_pos + 5;  // "(tool" 长度
            // 如果 "(tool" 后面可能继续（末尾、空格、-、字母），等待更多内容
            if (*after == '\0' || *after == ' ' || *after == '-' || 
                (*after >= 'a' && *after <= 'z')) {
                return false;  // 不完整，等待
            }
        }
        return true;  // 无 tool-calls，可以发送
    }
    
    // 检查 tool-calls 的括号是否闭合
    int paren_depth = 0;
    const char* p = tc_start;
    while (*p) {
        if (*p == '(') paren_depth++;
        else if (*p == ')') paren_depth--;
        p++;
    }
    
    return paren_depth == 0;  // 括号闭合则完整
}

// AI 流式回调函数：往所有支持流式的 channel 推送任务
static void stream_callback(const char* chunk, const char* accumulated, void* user_data) {
    (void)chunk;
    (void)user_data;
    
    // 检查 tool-calls 是否完整，不完整则等待更多内容
    if (!tool_calls_is_complete(accumulated)) {
        return;
    }
    
    if (!accumulated || strlen(accumulated) == 0) {
        return;
    }
    
    int current_len = strlen(accumulated);
    
    // 第一个 chunk：启动所有支持流式的 channel
    if (!g_stream_active) {
        g_stream_active = true;
        g_last_sent_len = current_len;
        clock_gettime(CLOCK_MONOTONIC, &g_last_update_time);
        
        StreamCallbackData data = { .content = accumulated };
        channels_foreach(stream_start_iterator, &data);
        return;
    }
    
    // 节流检查
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    
    long elapsed_ms = (now.tv_sec - g_last_update_time.tv_sec) * 1000 +
                       (now.tv_nsec - g_last_update_time.tv_nsec) / 1000000;
    int chars_added = current_len - g_last_sent_len;
    
    // 判断是否需要更新
    bool should_update = (elapsed_ms >= STREAM_MIN_INTERVAL_MS && chars_added > 0) ||
                          (chars_added >= STREAM_MIN_CHARS);
    
    if (should_update) {
        g_last_sent_len = current_len;
        g_last_update_time = now;
        
        log_debug("[TIMING] AI callback: elapsed=%ldms, chars_added=%d, len=%d", 
                  elapsed_ms, chars_added, current_len);
        
        StreamCallbackData data = { .content = accumulated };
        channels_foreach(stream_update_iterator, &data);
    }
}

// 结束流式消息
static void end_stream_message(void) {
    if (!g_stream_active) return;
    
    channels_foreach(stream_end_iterator, NULL);
    
    g_stream_active = false;
    g_last_sent_len = 0;
    g_last_update_time.tv_sec = 0;
    g_last_update_time.tv_nsec = 0;
    
    log_debug("[Stream] Ended stream message");
}

// Global agent manager
AgentNode* g_agent_node_list = NULL;
pthread_mutex_t g_agent_node_list_mutex = PTHREAD_MUTEX_INITIALIZER;

// Default agent node (for backward compatibility)
AgentNode* g_default_agent_node = NULL;

// ==================== Agent Node Management ====================

AgentNode* agent_node_create(const char* id, const char* model) {
    AgentNode* node = (AgentNode*)calloc(1, sizeof(AgentNode));
    if (!node) {
        log_error("Failed to allocate agent node\n");
        return NULL;
    }
    
    node->id = strdup(id);
    node->agent.model = strdup(model ? model : "default");
    node->agent.running = false;
    node->agent.debug_mode = false;
    node->agent.status = AGENT_STATUS_IDLE;
    node->agent.step_capacity = 10;
    node->agent.steps = (Step*)calloc(node->agent.step_capacity, sizeof(Step));
    node->worker_running = false;
    
    // Initialize message queue
    node->agent.message_queue = queue_init(100);
    if (!node->agent.message_queue) {
        log_error("Failed to create message queue\n");
        free(node->id);
        free(node->agent.model);
        free(node->agent.steps);
        free(node);
        return NULL;
    }
    
    // Add to global list
    pthread_mutex_lock(&g_agent_node_list_mutex);
    node->next = g_agent_node_list;
    g_agent_node_list = node;
    pthread_mutex_unlock(&g_agent_node_list_mutex);
    
    return node;
}

void agent_node_destroy(AgentNode* node) {
    if (!node) return;
    
    // Stop worker thread
    if (node->worker_running) {
        agent_node_stop_worker(node);
    }
    
    // Cleanup components
    if (node->agent.message_queue) {
        queue_destroy(node->agent.message_queue);
    }
    if (node->agent.session_manager) {
        session_manager_destroy(node->agent.session_manager);
    }
    if (node->agent.tool_registry) {
        tool_registry_destroy(node->agent.tool_registry);
    }
    if (node->agent.memory_manager) {
        memory_manager_destroy(node->agent.memory_manager);
    }
    
    // Remove from global list
    pthread_mutex_lock(&g_agent_node_list_mutex);
    AgentNode** current = &g_agent_node_list;
    while (*current) {
        if (*current == node) {
            *current = node->next;
            break;
        }
        current = &(*current)->next;
    }
    pthread_mutex_unlock(&g_agent_node_list_mutex);
    
    // Free resources
    free(node->id);
    free(node->agent.model);
    free(node->agent.error_message);
    free(node->agent.steps);
    free(node);
}

AgentNode* agent_node_get(const char* id) {
    pthread_mutex_lock(&g_agent_node_list_mutex);
    AgentNode* node = g_agent_node_list;
    while (node) {
        if (strcmp(node->id, id) == 0) {
            pthread_mutex_unlock(&g_agent_node_list_mutex);
            return node;
        }
        node = node->next;
    }
    pthread_mutex_unlock(&g_agent_node_list_mutex);
    return NULL;
}

void agent_node_list_all(void) {
    pthread_mutex_lock(&g_agent_node_list_mutex);
    AgentNode* node = g_agent_node_list;
    printf("Active Agents:\n");
    while (node) {
        printf("  - %s (model: %s, status: %d)\n", 
               node->id, node->agent.model, node->agent.status);
        node = node->next;
    }
    pthread_mutex_unlock(&g_agent_node_list_mutex);
}

bool agent_node_system_init(void) {
    pthread_mutex_init(&g_agent_node_list_mutex, NULL);
    return true;
}

void agent_node_system_cleanup(void) {
    pthread_mutex_lock(&g_agent_node_list_mutex);
    AgentNode* node = g_agent_node_list;
    while (node) {
        AgentNode* next = node->next;
        pthread_mutex_unlock(&g_agent_node_list_mutex);
        agent_node_destroy(node);
        pthread_mutex_lock(&g_agent_node_list_mutex);
        node = next;
    }
    g_agent_node_list = NULL;
    pthread_mutex_unlock(&g_agent_node_list_mutex);
    pthread_mutex_destroy(&g_agent_node_list_mutex);
}

// ==================== Worker Thread Management ====================

bool agent_node_start_worker(AgentNode* node) {
    if (!node) {
        log_error("agent_node_start_worker: node is NULL\n");
        return false;
    }
    
    if (node->worker_running) {
        log_warn("agent_node_start_worker: worker already running for node %s\n", node->id);
        return false;
    }
    
    log_info("Starting worker thread for agent node: %s", node->id);
    
    node->worker_running = true;
    int ret = pthread_create(&node->worker_thread, NULL, agent_node_worker_thread, node);
    if (ret != 0) {
        log_error("Failed to create worker thread: %d\n", ret);
        switch (ret) {
            case EAGAIN: log_error("Error: Resource temporarily unavailable\n"); break;
            case EINVAL: log_error("Error: Invalid argument\n"); break;
            case EPERM: log_error("Error: Operation not permitted\n"); break;
            default: log_error("Error: Unknown error\n"); break;
        }
        node->worker_running = false;
        return false;
    }
    
    log_info("Worker thread started successfully for node: %s", node->id);
    return true;
}

void agent_node_stop_worker(AgentNode* node) {
    if (!node || !node->worker_running) return;
    
    node->worker_running = false;
    pthread_join(node->worker_thread, NULL);
}


// Parse tool calls from JSON array
static ToolCallList* parse_tool_calls_from_json(cJSON* tool_calls_json) {
    if (!tool_calls_json || !cJSON_IsArray(tool_calls_json)) {
        return NULL;
    }
    
    ToolCallList* list = (ToolCallList*)malloc(sizeof(ToolCallList));
    if (!list) {
        return NULL;
    }
    
    list->count = cJSON_GetArraySize(tool_calls_json);
    list->calls = (ToolCall*)malloc(sizeof(ToolCall) * list->count);
    if (!list->calls) {
        free(list);
        return NULL;
    }
    
    for (int i = 0; i < list->count; i++) {
        cJSON* tool_call = cJSON_GetArrayItem(tool_calls_json, i);
        if (!tool_call) {
            continue;
        }
        
        cJSON* id = cJSON_GetObjectItem(tool_call, "id");
        cJSON* function = cJSON_GetObjectItem(tool_call, "function");
        
        if (id && cJSON_IsString(id) && function && cJSON_IsObject(function)) {
            list->calls[i].id = strdup(id->valuestring);
            
            cJSON* name = cJSON_GetObjectItem(function, "name");
            cJSON* arguments = cJSON_GetObjectItem(function, "arguments");
            
            if (name && cJSON_IsString(name)) {
                list->calls[i].name = strdup(name->valuestring);
            } else {
                list->calls[i].name = strdup("unknown");
            }
            
            // Parse arguments into ToolArgs
            list->calls[i].args = (ToolArgs*)malloc(sizeof(ToolArgs));
            if (list->calls[i].args) {
                cJSON* args_obj = NULL;
                if (arguments && cJSON_IsString(arguments)) {
                    args_obj = cJSON_Parse(arguments->valuestring);
                } else if (arguments && cJSON_IsObject(arguments)) {
                    args_obj = arguments;
                }
                
                if (args_obj && cJSON_IsObject(args_obj)) {
                    int arg_count = cJSON_GetArraySize(args_obj);
                    list->calls[i].args->count = arg_count;
                    list->calls[i].args->args = (ToolArg*)calloc(arg_count, sizeof(ToolArg));
                    
                    int idx = 0;
                    cJSON* item = NULL;
                    cJSON_ArrayForEach(item, args_obj) {
                        if (idx < arg_count) {
                            list->calls[i].args->args[idx].key = strdup(item->string);
                            if (cJSON_IsString(item)) {
                                list->calls[i].args->args[idx].value = strdup(item->valuestring);
                            } else {
                                char* val_str = cJSON_Print(item);
                                list->calls[i].args->args[idx].value = val_str ? val_str : strdup("");
                            }
                            idx++;
                        }
                    }
                } else {
                    list->calls[i].args->count = 0;
                    list->calls[i].args->args = NULL;
                }
                
                if (args_obj && args_obj != arguments) {
                    cJSON_Delete(args_obj);
                }
            }
        } else {
            list->calls[i].id = strdup("");
            list->calls[i].name = strdup("unknown");
            list->calls[i].args = NULL;
        }
    }
    
    return list;
}

// Parse S-expression tool calls
// Format: (tool-calls (tool_name arg1 arg2 ...) ...)
static ToolCallList* parse_tool_calls_from_sexp(const char* content) {
    if (!content) return NULL;
    
    // Find (tool-calls ...)
    const char* tc_start = strstr(content, "(tool-calls");
    if (!tc_start) return NULL;
    
    // Find the opening parenthesis after tool-calls
    const char* ptr = tc_start + 11; // skip "(tool-calls"
    while (*ptr && (*ptr == ' ' || *ptr == '\t' || *ptr == '\n')) ptr++;
    
    if (*ptr != '(') return NULL;
    
    // Count tool calls
    int count = 0;
    const char* scan = ptr;
    while (*scan) {
        if (*scan == '(') {
            scan++;
            while (*scan && (*scan == ' ' || *scan == '\t' || *scan == '\n')) scan++;
            if (*scan && *scan != ')') count++;
        }
        scan++;
    }
    
    if (count == 0) return NULL;
    
    ToolCallList* list = (ToolCallList*)malloc(sizeof(ToolCallList));
    if (!list) return NULL;
    
    list->count = count;
    list->calls = (ToolCall*)calloc(count, sizeof(ToolCall));
    if (!list->calls) {
        free(list);
        return NULL;
    }
    
    // Parse each tool call
    int call_idx = 0;
    ptr = tc_start + 11;
    while (*ptr && call_idx < count) {
        while (*ptr && (*ptr == ' ' || *ptr == '\t' || *ptr == '\n')) ptr++;
        
        if (*ptr != '(') {
            ptr++;
            continue;
        }
        
        ptr++; // skip '('
        while (*ptr && (*ptr == ' ' || *ptr == '\t' || *ptr == '\n')) ptr++;
        
        if (*ptr == ')') {
            ptr++;
            continue;
        }
        
        // Extract tool name
        const char* name_start = ptr;
        while (*ptr && *ptr != ' ' && *ptr != '\t' && *ptr != '\n' && *ptr != ')') ptr++;
        
        size_t name_len = ptr - name_start;
        list->calls[call_idx].name = (char*)malloc(name_len + 1);
        strncpy(list->calls[call_idx].name, name_start, name_len);
        list->calls[call_idx].name[name_len] = '\0';
        
        // Generate a call ID
        list->calls[call_idx].id = (char*)malloc(20);
        snprintf(list->calls[call_idx].id, 20, "call_%d", call_idx + 1);
        
        // Count arguments first
        int arg_count = 0;
        const char* arg_scan = ptr;
        while (*arg_scan && *arg_scan != ')') {
            while (*arg_scan && (*arg_scan == ' ' || *arg_scan == '\t' || *arg_scan == '\n')) arg_scan++;
            if (*arg_scan && *arg_scan != ')') {
                arg_count++;
                // Skip the argument
                if (*arg_scan == '"') {
                    arg_scan++;
                    while (*arg_scan && *arg_scan != '"') arg_scan++;
                    if (*arg_scan == '"') arg_scan++;
                } else {
                    while (*arg_scan && *arg_scan != ' ' && *arg_scan != '\t' && *arg_scan != '\n' && *arg_scan != ')') arg_scan++;
                }
            }
        }
        
        // Allocate ToolArgs
        list->calls[call_idx].args = (ToolArgs*)malloc(sizeof(ToolArgs));
        if (list->calls[call_idx].args && arg_count > 0) {
            list->calls[call_idx].args->count = 0;
            list->calls[call_idx].args->args = (ToolArg*)calloc(arg_count, sizeof(ToolArg));
        } else if (list->calls[call_idx].args) {
            list->calls[call_idx].args->count = 0;
            list->calls[call_idx].args->args = NULL;
        }
        
        // Parse arguments into ToolArgs
        while (*ptr && *ptr != ')' && list->calls[call_idx].args) {
            while (*ptr && (*ptr == ' ' || *ptr == '\t' || *ptr == '\n')) ptr++;
            if (*ptr == ')') break;
            
            ToolArg* current_arg = &list->calls[call_idx].args->args[list->calls[call_idx].args->count];
            
            if (*ptr == '"') {
                // String argument (positional) - use "arg" as key
                ptr++;
                const char* arg_start = ptr;
                while (*ptr && *ptr != '"') ptr++;
                size_t arg_len = ptr - arg_start;
                current_arg->key = strdup("arg");
                current_arg->value = (char*)malloc(arg_len + 1);
                strncpy(current_arg->value, arg_start, arg_len);
                current_arg->value[arg_len] = '\0';
                if (*ptr == '"') ptr++;
                list->calls[call_idx].args->count++;
            } else {
                // Non-string argument (could be keyword or value)
                const char* arg_start = ptr;
                while (*ptr && *ptr != ' ' && *ptr != '\t' && *ptr != '\n' && *ptr != ')') ptr++;
                size_t arg_len = ptr - arg_start;
                char* arg_value = (char*)malloc(arg_len + 1);
                strncpy(arg_value, arg_start, arg_len);
                arg_value[arg_len] = '\0';
                
                // Check if next non-space is a value
                const char* next = ptr;
                while (*next && (*next == ' ' || *next == '\t' || *next == '\n')) next++;
                
                if (*next && *next != ')' && *next != '(') {
                    // This is a keyword, next is the value
                    ptr = (char*)next;
                    current_arg->key = arg_value;
                    
                    if (*ptr == '"') {
                        ptr++;
                        const char* val_start = ptr;
                        while (*ptr && *ptr != '"') ptr++;
                        size_t val_len = ptr - val_start;
                        current_arg->value = (char*)malloc(val_len + 1);
                        strncpy(current_arg->value, val_start, val_len);
                        current_arg->value[val_len] = '\0';
                        if (*ptr == '"') ptr++;
                    } else {
                        const char* val_start = ptr;
                        while (*ptr && *ptr != ' ' && *ptr != '\t' && *ptr != '\n' && *ptr != ')') ptr++;
                        size_t val_len = ptr - val_start;
                        current_arg->value = (char*)malloc(val_len + 1);
                        strncpy(current_arg->value, val_start, val_len);
                        current_arg->value[val_len] = '\0';
                    }
                    list->calls[call_idx].args->count++;
                } else {
                    // Single value argument
                    current_arg->key = strdup("arg");
                    current_arg->value = arg_value;
                    list->calls[call_idx].args->count++;
                }
            }
        }
        
        if (*ptr == ')') ptr++;
        call_idx++;
    }
    
    return list;
}

// Parse AI response and extract tool calls
// Returns ToolCallList if tool calls found, NULL otherwise
static ToolCallList* parse_response_tool_calls(AIModelResponse* response) {
    if (!response || !response->success) return NULL;
    
    ToolCallList* tool_call_list = NULL;
    
    // 1. Check response->tool_calls (OpenAI native tool_calls format)
    if (response->tool_calls && strlen(response->tool_calls) > 0) {
        log_debug("Found tool_calls in response.tool_calls: %s", response->tool_calls);
        cJSON* tool_calls_json = cJSON_Parse(response->tool_calls);
        if (tool_calls_json) {
            tool_call_list = parse_tool_calls_from_json(tool_calls_json);
            cJSON_Delete(tool_calls_json);
        }
    }
    
    // 2. Check for S-expression tool calls: (tool-calls ...)
    if (!tool_call_list && response->content && strlen(response->content) > 0) {
        if (strstr(response->content, "(tool-calls")) {
            log_debug("Found S-expression tool_calls in response.content");
            tool_call_list = parse_tool_calls_from_sexp(response->content);
        }
    }
    
    return tool_call_list;
}

// Check if response content contains JSON tool_calls (legacy format detection)
static bool response_has_json_tool_calls(const char* content) {
    if (!content || strlen(content) == 0) return false;
    
    cJSON* content_root = cJSON_Parse(content);
    if (!content_root) {
        // Try to fix common JSON issues
        char* fixed_content = strdup(content);
        if (fixed_content) {
            int brace_count = 0, bracket_count = 0;
            for (char* p = fixed_content; *p; p++) {
                if (*p == '{') brace_count++;
                else if (*p == '}') brace_count--;
                else if (*p == '[') bracket_count++;
                else if (*p == ']') bracket_count--;
            }
            
            if (brace_count < 0 || bracket_count < 0) {
                int len = strlen(fixed_content);
                while (len > 0 && (brace_count < 0 || bracket_count < 0)) {
                    if (fixed_content[len-1] == '}') { brace_count++; fixed_content[--len] = '\0'; }
                    else if (fixed_content[len-1] == ']') { bracket_count++; fixed_content[--len] = '\0'; }
                    else break;
                }
                content_root = cJSON_Parse(fixed_content);
            }
            free(fixed_content);
        }
    }
    
    bool has_tool_calls = false;
    if (content_root) {
        cJSON* tc = cJSON_GetObjectItem(content_root, "tool_calls");
        has_tool_calls = (tc && cJSON_IsArray(tc));
        cJSON_Delete(content_root);
    }
    
    return has_tool_calls;
}

// Worker thread function
void* agent_node_worker_thread(void* arg) {
    AgentNode* node = (AgentNode*)arg;
    Agent* agent = &node->agent;
    
    log_info("Agent node worker thread started (node_id: %s)", node->id);
    
    if (g_config.debug) {
        log_debug("Agent node worker thread started\n");
    }
    
    while (node->worker_running) {
        // 1. Dequeue message
        QueueItem* item = queue_dequeue(agent->message_queue, 1000);
        if (!item) continue;
        
        if (g_config.debug) {
            log_debug("Dequeued message: %s\n", item->message->content);
        }
        
        // 2. Get or create session
        Session* session = session_manager_get_or_create(
            agent->session_manager, item->session_key);
        if (!session) {
            log_error("Failed to get or create session\n");
            queue_item_destroy(item);
            continue;
        }
        
        log_info("Session %s has %d messages in history", session->session_id, session->history->count);
        
        if (g_config.debug) {
            log_debug("Got or created session: %s\n", session->session_id);
        }
        
        // 3. Add user message to session
        session_add_message(session, item->message);
        // Set message to NULL so it won't be destroyed by queue_item_destroy
        item->message = NULL;
        
        if (g_config.debug) {
            log_debug("Added user message to session\n");
        }
        
        // Check if session history is too large and trim if necessary
        int max_history = g_config.session.max_history_per_session > 0 ? 
                         g_config.session.max_history_per_session : 100;
        if (session->history->count > max_history) {
            log_info("Session history too large (%d messages), trimming to %d\n", 
                    session->history->count, max_history);
            // Remove oldest messages
            int remove_count = session->history->count - max_history;
            for (int i = 0; i < remove_count; i++) {
                message_destroy(session->history->messages[0]);
                // Shift remaining messages
                for (int j = 1; j < session->history->count; j++) {
                    session->history->messages[j-1] = session->history->messages[j];
                }
                session->history->count--;
            }
        }
        
        // 4. Build context
        MessageList* context = message_list_create();
        if (context) {
            // Load session history with limit (context_history_limit)
            int limit = g_config.session.context_history_limit > 0 ? 
                        g_config.session.context_history_limit : 5;
            int start_idx = session->history->count > limit ? 
                           session->history->count - limit : 0;
            
            for (int i = start_idx; i < session->history->count; i++) {
                message_list_append(context, session->history->messages[i]);
            }
            
            if (g_config.debug) {
                log_debug("Built context with %d messages (limit: %d, total history: %d)\n", 
                         context->count, limit, session->history->count);
            }
            
            // 5. Check and execute compaction (TODO)
            
            // 6. Agent loop (ReAct paradigm: Think -> Act -> Observe)
            int max_iterations = 10;
            for (int i = 0; i < max_iterations; i++) {
                if (g_config.debug) {
                    log_debug("Agent loop iteration %d\n", i + 1);
                }
                
                // 6.1 Think: Call AI model with current context
                if (g_config.debug) {
                    log_debug("Calling AI model with %d messages in context\n", context->count);
                }
                
                // 设置流式回调（如果 AI 配置了 stream 模式）
                AIProvider* provider = ai_model_get_provider();
                if (provider && provider->config.stream) {
                    ai_model_set_stream_callback(stream_callback, NULL);
                    g_stream_active = false;  // 重置流式状态
                }
                
                // Use configured system prompt or default
                // TODO: Fix g_config.agent.system_prompt corruption issue
                // For now, always use default system prompt
                (void)g_config.agent.system_prompt;  // Suppress unused warning
                const char* system_prompt = DEFAULT_SYSTEM_PROMPT;
                log_debug("Using DEFAULT_SYSTEM_PROMPT: %p", (void*)system_prompt);
                AIModelResponse* response = ai_model_send_messages(context, system_prompt);
                
                // AI 调用结束后清理流式回调
                ai_model_set_stream_callback(NULL, NULL);
                
                if (!response) {
                    if (g_config.debug) {
                        log_debug("No response from AI model\n");
                    }
                    end_stream_message();  // 结束流式消息
                    break;
                }
                
                if (response->success) {
                    if (g_config.debug) {
                        log_debug("AI model returned success\n");
                    }
                    
                    // 6.2 Check if content contains JSON tool_calls (for display control)
                    bool has_tool_calls_in_content = response_has_json_tool_calls(response->content);
                    
                    // 6.3 Act: Add assistant message to session
                    Message* assistant_msg = message_create(ROLE_ASSISTANT, response->content);
                    session_add_message(session, assistant_msg);
                    
                    // Only print response if it's not a tool_calls JSON
                    if (!has_tool_calls_in_content && !strstr(response->content ? response->content : "", "(tool-calls")) {
                        printf("\n[AI Response]: %s\n\n", response->content ? response->content : "");
                    }
                    
                    // 6.4 Observe: Parse tool calls from response
                    if (g_config.debug) {
                        log_debug("Parsing tool calls from response\n");
                    }
                    
                    ToolCallList* tool_call_list = parse_response_tool_calls(response);
                    
                    if (tool_call_list && tool_call_list->count > 0) {
                        // Execute tool calls
                        if (g_config.debug) {
                            log_debug("Executing %d tool calls\n", tool_call_list->count);
                        }
                        
                        for (int j = 0; j < tool_call_list->count; j++) {
                            ToolCall* call = &tool_call_list->calls[j];
                            char* result = NULL;
                            int result_len = 0;
                            
                            if (g_config.debug) {
                                log_debug("Executing tool: '%s'\n", call->name);
                            }
                            
                            // Execute tool
                            Tool* tool = tool_registry_get(agent->tool_registry, call->name);
                            if (tool) {
                                if (g_config.debug) {
                                    log_debug("Tool found: %s\n", tool->name);
                                }
                                tool->execute(call->args, &result, &result_len);
                            } else {
                                if (g_config.debug) {
                                    log_debug("Tool not found: '%s'\n", call->name);
                                }
                                result = strdup("Error: Tool not found");
                                result_len = strlen(result);
                            }
                            
                            // Add tool result to session
                            if (g_config.debug) {
                                log_debug("Tool result: %s\n", result ? result : "(null)");
                            }
                            Message* tool_msg = message_create_tool(call->id, call->name, result ? result : "Error executing tool");
                            session_add_message(session, tool_msg);
                            
                            if (result) {
                                free(result);
                            }
                        }
                        
                        // Free tool calls
                        for (int j = 0; j < tool_call_list->count; j++) {
                            free(tool_call_list->calls[j].id);
                            free(tool_call_list->calls[j].name);
                            if (tool_call_list->calls[j].args) {
                                for (int k = 0; k < tool_call_list->calls[j].args->count; k++) {
                                    free(tool_call_list->calls[j].args->args[k].key);
                                    free(tool_call_list->calls[j].args->args[k].value);
                                }
                                free(tool_call_list->calls[j].args->args);
                                free(tool_call_list->calls[j].args);
                            }
                        }
                        free(tool_call_list->calls);
                        free(tool_call_list);
                        
                        // Update context for next iteration
                        message_list_destroy(context);
                        context = message_list_create();
                        for (int i = 0; i < session->history->count; i++) {
                            message_list_append(context, session->history->messages[i]);
                        }
                        
                        // Continue loop to get model's response to tool results
                        ai_model_free_response(response);
                        continue;
                    } else {
                        // No tool calls, send response to all connected channels
                        if (g_config.debug) {
                            log_debug("Sending message to all connected channels\n");
                        }

                        // 检查是否使用了流式模式
                        bool used_stream = g_stream_active;

                        // 结束流式消息（如果流式模式已启动）
                        end_stream_message();

                        // 只有非流式模式才发送完整消息
                        // 流式模式下消息已经在回调中发送
                        if (!used_stream) {
                            channel_send_message_to_all(response->content);
                        }

                        ai_model_free_response(response);
                        break;
                    }
                } else {
                    printf("\n[AI Error]: %s\n\n", response->error);
                    end_stream_message();  // 结束流式消息
                    ai_model_free_response(response);
                    break;
                }
            }
            
            message_list_destroy(context);
            
            if (g_config.debug) {
                log_debug("Destroyed context\n");
            }
        }
        
        // 7. Save session
        if (g_config.debug) {
            //log_debug("Saving session: %s", session->session_id);
        }
        
        // Build sessions directory path
        char sessions_dir[512];
        snprintf(sessions_dir, sizeof(sessions_dir), "%s/sessions", g_config.workspace_path);
        session_save(session, sessions_dir);
        
        // Cleanup
        queue_item_destroy(item);
        
        if (g_config.debug) {
            log_debug("Cleaned up queue item");
        }
    }
    
    if (g_config.debug) {
        log_debug("Agent node worker thread exiting");
    }
    
    return NULL;
}
