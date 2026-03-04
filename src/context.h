#ifndef CONTEXT_H
#define CONTEXT_H

#include <stdbool.h>
#include <pthread.h>
#include "agent.h"

// Forward declaration
struct WorkerContext;

// Agent with ID and linked list support (for future multi-agent)
typedef struct AgentNode {
    char *id;                      // Agent unique identifier
    Agent agent;                   // Agent instance
    pthread_t worker_thread;       // Worker thread
    bool worker_running;           // Worker running flag
    struct AgentNode* next;        // Linked list pointer
} AgentNode;

// Global agent manager
extern AgentNode* g_agent_node_list;
extern pthread_mutex_t g_agent_node_list_mutex;

// Default agent node (for backward compatibility)
extern AgentNode* g_default_agent_node;

// ==================== Agent Node Management ====================

// Create a new Agent node
AgentNode* agent_node_create(const char* id, const char* model);

// Destroy an Agent node
void agent_node_destroy(AgentNode* node);

// Get Agent node by ID
AgentNode* agent_node_get(const char* id);

// List all agent nodes
void agent_node_list_all(void);

// Initialize agent node system
bool agent_node_system_init(void);

// Cleanup agent node system
void agent_node_system_cleanup(void);

// ==================== Worker Thread Management ====================

// Start worker thread for an agent node
bool agent_node_start_worker(AgentNode* node);

// Stop worker thread for an agent node
void agent_node_stop_worker(AgentNode* node);

// Worker thread function
void* agent_node_worker_thread(void* arg);

#endif // CONTEXT_H
