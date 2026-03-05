#include "context.h"
#include "config.h"
#include "channels.h"
#include "ai_model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

// Worker thread function
void* agent_node_worker_thread(void* arg) {
    AgentNode* node = (AgentNode*)arg;
    Agent* agent = &node->agent;
    
    while (node->worker_running) {
        // 1. Dequeue message
        QueueItem* item = queue_dequeue(agent->message_queue, 1000);
        if (!item) continue;
        
        // 2. Get or create session
        Session* session = session_manager_get_or_create(
            agent->session_manager, item->session_key);
        if (!session) {
            fprintf(stderr, "Failed to get or create session\n");
            queue_item_destroy(item);
            continue;
        }
        
        // 3. Add user message to session
        session_add_message(session, item->message);
        
        // 4. Build context
        MessageList* context = message_list_create();
        if (context) {
            // Load session history
            for (int i = 0; i < session->history->count; i++) {
                message_list_append(context, session->history->messages[i]);
            }
            
            // 5. Check and execute compaction (TODO)
            
            // 6. Agent loop
            int max_iterations = 10;
            for (int i = 0; i < max_iterations; i++) {
                AIModelResponse* response = ai_model_send_message(item->message->content);
                if (!response) break;
                
                if (response->success) {
                    Message* assistant_msg = message_create(ROLE_ASSISTANT, response->content);
                    session_add_message(session, assistant_msg);
                    
                    printf("\n[AI Response]: %s\n\n", response->content);
                    channel_send_message(CHANNEL_WEBCHAT, response->content);
                    
                    // TODO: Parse and execute tool calls
                    
                    ai_model_free_response(response);
                    break;
                } else {
                    printf("\n[AI Error]: %s\n\n", response->error);
                    ai_model_free_response(response);
                    break;
                }
            }
            
            message_list_destroy(context);
        }
        
        // 7. Save session
        session_save(session, g_config.workspace_path);
        
        // Cleanup
        queue_item_destroy(item);
    }
    
    return NULL;
}
