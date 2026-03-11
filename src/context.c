#include "context.h"
#include "config.h"
#include "channels.h"
#include "ai_model.h"
#include "agent.h"
#include "tool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
        fprintf(stderr, "Failed to allocate agent node\n");
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
        fprintf(stderr, "Failed to create message queue\n");
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
        fprintf(stderr, "Failed to create worker thread: %d\n", ret);
        switch (ret) {
            case EAGAIN: fprintf(stderr, "Error: Resource temporarily unavailable\n"); break;
            case EINVAL: fprintf(stderr, "Error: Invalid argument\n"); break;
            case EPERM: fprintf(stderr, "Error: Operation not permitted\n"); break;
            default: fprintf(stderr, "Error: Unknown error\n"); break;
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

// Parse tool calls from AI response
static ToolCallList* parse_tool_calls(const char* content) {
    // Simple JSON parsing for tool calls
    // This is a placeholder implementation
    return NULL;
}

// Worker thread function
void* agent_node_worker_thread(void* arg) {
    AgentNode* node = (AgentNode*)arg;
    Agent* agent = &node->agent;
    
    if (g_config.debug) {
        printf("[DEBUG] Agent node worker thread started\n");
    }
    
    while (node->worker_running) {
        // 1. Dequeue message
        QueueItem* item = queue_dequeue(agent->message_queue, 1000);
        if (!item) continue;
        
        if (g_config.debug) {
            printf("[DEBUG] Dequeued message: %s\n", item->message->content);
        }
        
        // 2. Get or create session
        Session* session = session_manager_get_or_create(
            agent->session_manager, item->session_key);
        if (!session) {
            fprintf(stderr, "Failed to get or create session\n");
            queue_item_destroy(item);
            continue;
        }
        
        if (g_config.debug) {
            printf("[DEBUG] Got or created session: %s\n", session->session_id);
        }
        
        // 3. Add user message to session
        // Save content pointer before setting message to NULL
        const char* message_content = item->message->content;
        session_add_message(session, item->message);
        // Set message to NULL so it won't be destroyed by queue_item_destroy
        item->message = NULL;
        
        if (g_config.debug) {
            printf("[DEBUG] Added user message to session\n");
        }
        
        // 4. Build context
        MessageList* context = message_list_create();
        if (context) {
            // Load session history
            for (int i = 0; i < session->history->count; i++) {
                message_list_append(context, session->history->messages[i]);
            }
            
            if (g_config.debug) {
                printf("[DEBUG] Built context with %d messages\n", context->count);
            }
            
            // 5. Check and execute compaction (TODO)
            
            // 6. Agent loop (ReAct paradigm: Think -> Act -> Observe)
            int max_iterations = 10;
            for (int i = 0; i < max_iterations; i++) {
                if (g_config.debug) {
                    printf("[DEBUG] Agent loop iteration %d\n", i + 1);
                }
                
                // 6.1 Think: Call AI model with current context
                if (g_config.debug) {
                    printf("[DEBUG] Calling AI model\n");
                }
                
                AIModelResponse* response = ai_model_send_message(message_content);
                if (!response) {
                    if (g_config.debug) {
                        printf("[DEBUG] No response from AI model\n");
                    }
                    break;
                }
                
                if (response->success) {
                    if (g_config.debug) {
                        printf("[DEBUG] AI model returned success\n");
                    }
                    
                    // 6.2 Act: Add assistant message to session
                    Message* assistant_msg = message_create(ROLE_ASSISTANT, response->content);
                    session_add_message(session, assistant_msg);
                    
                    printf("\n[AI Response]: %s\n\n", response->content);
                    
                    // 6.3 Observe: Check for tool calls
                    if (g_config.debug) {
                        printf("[DEBUG] Checking for tool calls\n");
                    }
                    
                    ToolCallList* tool_call_list = parse_tool_calls(response->content);
                    if (tool_call_list && tool_call_list->count > 0) {
                        // Execute tool calls
                        if (g_config.debug) {
                            printf("[DEBUG] Executing %d tool calls\n", tool_call_list->count);
                        }
                        
                        for (int j = 0; j < tool_call_list->count; j++) {
                            ToolCall* call = &tool_call_list->calls[j];
                            char* result = NULL;
                            int result_len = 0;
                            
                            if (g_config.debug) {
                                printf("[DEBUG] Executing tool: %s with args: %s\n", call->name, call->arguments);
                            }
                            
                            // Execute tool
                            Tool* tool = tool_registry_get(agent->tool_registry, call->name);
                            int status = -1;
                            if (tool) {
                                status = tool->execute(call->arguments, &result, &result_len);
                            } else {
                                result = strdup("Error: Tool not found");
                                result_len = strlen(result);
                            }
                            
                            // Add tool result to session
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
                            printf("[DEBUG] Sending message to WebChat\n");
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
                printf("[DEBUG] Destroyed context\n");
            }
        }
        
        // 7. Save session
        if (g_config.debug) {
            printf("[DEBUG] Saving session: %s\n", session->session_id);
        }
        session_save(session, g_config.workspace_path);
        
        // Cleanup
        queue_item_destroy(item);
        
        if (g_config.debug) {
            printf("[DEBUG] Cleaned up queue item\n");
        }
    }
    
    if (g_config.debug) {
        printf("[DEBUG] Agent node worker thread exiting\n");
    }
    
    return NULL;
}
