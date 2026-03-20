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

// Default system prompt for the agent
static const char* DEFAULT_SYSTEM_PROMPT = 
"你是一个有用的助手，可以帮助用户完成各种任务。请保持对话的连贯性，基于上下文进行回复。\n"
"\n"
"可用工具（必须严格使用以下名称）：\n"
"1. get_weather - 获取天气信息\n"
"   参数: location (string) - 城市名称，如 \"北京\"、\"上海\"\n"
"   注意：工具名称必须是 get_weather，不能简写为 weather\n"
"\n"
"2. web_search - 搜索网络信息\n"
"   参数: query (string) - 搜索关键词\n"
"\n"
"3. calculator - 计算数学表达式\n"
"   参数: expression (string) - 数学表达式，如 \"1+2*3\"\n"
"\n"
"4. time - 获取当前时间\n"
"   参数: 无\n"
"\n"
"5. read_file - 读取文件内容\n"
"   参数: path (string) - 文件路径\n"
"\n"
"6. write_file - 写入文件内容\n"
"   参数: path (string) - 文件路径, content (string) - 文件内容\n"
"\n"
"7. reverse_string - 反转字符串\n"
"   参数: text (string) - 要反转的文本\n"
"\n"
"8. memory_save - 保存信息到内存\n"
"   参数: key (string) - 键名, value (string) - 值\n"
"\n"
"9. memory_load - 从内存读取信息\n"
"   参数: key (string) - 键名\n"
"\n"
"10. list_directory - 查看目录文件列表\n"
"   参数: path (string) - 目录路径，支持波浪号展开，如 \".\"、\"~\"、\"~/.catclaw\"、\"/home/user\"\n"
"   注意：~ 会被自动展开为用户主目录\n"
"\n"
"重要：function.name 必须是上述工具名称之一，严格区分大小写！\n"
"\n"
"如果需要使用工具，请输出以下 JSON 格式（注意：arguments 必须是 JSON 字符串，需要转义）：\n"
"{\"tool_calls\": [{\"id\": \"call_1\", \"type\": \"function\", \"function\": {\"name\": \"工具名\", \"arguments\": \"{\\\"参数名\\\": \\\"参数值\\\"}\"}}]}\n"
"\n"
"注意：function.arguments 是一个字符串，不是对象！必须用双引号包裹，内部引号需要转义。\n"
"\n"
"工具执行后会返回结果，格式为 [TOOL_RESULT] 结果 [/TOOL_RESULT]。\n"
"[TOOL_RESULT] 是系统消息，不是用户输入。\n"
"当你看到 [TOOL_RESULT] 时，说明工具已经执行完成，你必须直接生成最终回复，不要再输出 tool_calls！\n"
"\n"
"示例1 - 调用工具：\n"
"用户：北京天气怎么样？\n"
"助手：{\"tool_calls\": [{\"id\": \"call_1\", \"type\": \"function\", \"function\": {\"name\": \"get_weather\", \"arguments\": \"{\\\"location\\\": \\\"北京\\\"}\"}}]}\n"
"\n"
"示例2 - 回复用户（看到 [TOOL_RESULT] 后）：\n"
"用户：北京天气怎么样？\n"
"助手：{\"tool_calls\": [{\"id\": \"call_1\", \"type\": \"function\", \"function\": {\"name\": \"get_weather\", \"arguments\": \"{\\\"location\\\": \\\"北京\\\"}\"}}]}\n"
"[TOOL_RESULT] 北京天气：22°C，晴朗，湿度：45% [/TOOL_RESULT]\n"
"助手：北京今天天气晴朗，温度22°C，湿度45%，很适合外出活动！\n"
"\n"
"重要提醒：\n"
"1. 只有需要工具时才输出 tool_calls\n"
"2. 看到 [TOOL_RESULT] 后，直接回复用户，绝对不要再输出 tool_calls！\n"
"3. 不要自己编造 [TOOL_RESULT]，必须等待系统返回！";
#include "common/log.h"

// ToolCall structure for tool execution
typedef struct {
    char* id;
    char* name;
    char* arguments;
} ToolCall;

// ToolCallList structure
typedef struct {
    ToolCall* calls;
    int count;
} ToolCallList;

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
    if (!node || node->worker_running) return false;
    
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
        cJSON* type = cJSON_GetObjectItem(tool_call, "type");
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
            
            if (arguments && cJSON_IsString(arguments)) {
                // arguments is already a JSON string
                list->calls[i].arguments = strdup(arguments->valuestring);
            } else if (arguments && cJSON_IsObject(arguments)) {
                // arguments is a JSON object, convert to string
                char* args_str = cJSON_Print(arguments);
                if (args_str) {
                    list->calls[i].arguments = args_str;
                } else {
                    list->calls[i].arguments = strdup("{}");
                }
            } else {
                list->calls[i].arguments = strdup("{}");
            }
        } else {
            list->calls[i].id = strdup("");
            list->calls[i].name = strdup("unknown");
            list->calls[i].arguments = strdup("{}");
        }
    }
    
    return list;
}

// Worker thread function
void* agent_node_worker_thread(void* arg) {
    AgentNode* node = (AgentNode*)arg;
    Agent* agent = &node->agent;
    
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
        // Save content pointer before setting message to NULL
        const char* message_content = item->message->content;
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
                
                // Use configured system prompt or default
                // TODO: Fix g_config.agent.system_prompt corruption issue
                // For now, always use default system prompt
                (void)g_config.agent.system_prompt;  // Suppress unused warning
                const char* system_prompt = DEFAULT_SYSTEM_PROMPT;
                log_debug("Using DEFAULT_SYSTEM_PROMPT: %p", (void*)system_prompt);
                AIModelResponse* response = ai_model_send_messages(context, system_prompt);
                if (!response) {
                    if (g_config.debug) {
                        log_debug("No response from AI model\n");
                    }
                    break;
                }
                
                if (response->success) {
                    if (g_config.debug) {
                        log_debug("AI model returned success\n");
                    }
                    
                    // 6.2 Pre-check: Detect if content is a tool_calls JSON
                    bool has_tool_calls_in_content = false;
                    log_debug("response->content: '%s'", response->content ? response->content : "(null)");
                    log_debug("response->tool_calls: '%s'", response->tool_calls ? response->tool_calls : "(null)");
                    if (response->content && strlen(response->content) > 0) {
                        cJSON *content_root = cJSON_Parse(response->content);
                        if (content_root) {
                            cJSON *tc = cJSON_GetObjectItem(content_root, "tool_calls");
                            if (tc && cJSON_IsArray(tc)) {
                                has_tool_calls_in_content = true;
                            }
                            cJSON_Delete(content_root);
                        }
                    }
                    
                    // 6.3 Act: Add assistant message to session
                    Message* assistant_msg = message_create(ROLE_ASSISTANT, response->content);
                    session_add_message(session, assistant_msg);
                    
                    // Only print response if it's not a tool_calls JSON
                    if (!has_tool_calls_in_content) {
                        printf("\n[AI Response]: %s\n\n", response->content);
                    }
                    
                    // 6.4 Observe: Check for tool calls
                    if (g_config.debug) {
                        log_debug("Checking for tool calls\n");
                    }
                    
                    ToolCallList* tool_call_list = NULL;
                    
                    // Parse tool_calls from AI model response
                    // 1. Check response->tool_calls (OpenAI native tool_calls format)
                    if (response->tool_calls && strlen(response->tool_calls) > 0) {
                        if (g_config.debug) {
                            log_debug("Found tool_calls in response.tool_calls: %s\n", response->tool_calls);
                        }
                        cJSON *tool_calls_json = cJSON_Parse(response->tool_calls);
                        if (tool_calls_json) {
                            tool_call_list = parse_tool_calls_from_json(tool_calls_json);
                            cJSON_Delete(tool_calls_json);
                        }
                    }
                    
                    // 2. Check response->content for text-based tool_calls JSON: {"tool_calls": [...]}
                    if (!tool_call_list && response->content && strlen(response->content) > 0) {
                        cJSON *content_root = cJSON_Parse(response->content);
                        if (content_root) {
                            cJSON *tool_calls_arr = cJSON_GetObjectItem(content_root, "tool_calls");
                            if (tool_calls_arr && cJSON_IsArray(tool_calls_arr)) {
                                if (g_config.debug) {
                                    log_debug("Found tool_calls in response.content: %s\n", response->content);
                                }
                                tool_call_list = parse_tool_calls_from_json(tool_calls_arr);
                            }
                            cJSON_Delete(content_root);
                        }
                    }
                    
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
                                log_debug("Executing tool: '%s' with args: %s\n", call->name, call->arguments);
                            }
                            
                            // Execute tool
                            Tool* tool = tool_registry_get(agent->tool_registry, call->name);
                            int status = -1;
                            if (tool) {
                                if (g_config.debug) {
                                    log_debug("Tool found: %s\n", tool->name);
                                    log_debug("Tool arguments: %s\n", call->arguments);
                                }
                                status = tool->execute(call->arguments, &result, &result_len);
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
                            free(tool_call_list->calls[j].arguments);
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
                        // No tool calls, send response to user
                        if (g_config.debug) {
                            log_debug("Sending message to WebChat\n");
                        }
                        channel_send_message(CHANNEL_WEBCHAT, response->content);
                        
                        ai_model_free_response(response);
                        break;
                    }
                } else {
                    printf("\n[AI Error]: %s\n\n", response->error);
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
            log_debug("Saving session: %s", session->session_id);
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
