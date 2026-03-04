#ifndef QUEUE_H
#define QUEUE_H

#include <pthread.h>
#include "message.h"

// Queue mode enumeration
typedef enum {
    QUEUE_MODE_COLLECT,     // 收集模式（默认）
    QUEUE_MODE_STEER,       // 引导模式
    QUEUE_MODE_FOLLOWUP     // 后续模式
} QueueMode;

// Queue item structure
typedef struct {
    char* session_key;
    Message* message;
    QueueMode mode;
    long long enqueue_time;
} QueueItem;

// Message queue structure
typedef struct {
    QueueItem* items;
    int front;
    int rear;
    int count;
    int capacity;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} MessageQueue;

// Functions
QueueItem* queue_item_create(const char* session_key, Message* message, QueueMode mode);
void queue_item_destroy(QueueItem* item);

MessageQueue* queue_init(int capacity);
void queue_destroy(MessageQueue* queue);

bool queue_enqueue(MessageQueue* queue, QueueItem* item);
QueueItem* queue_dequeue(MessageQueue* queue, int timeout_ms);

bool queue_is_empty(MessageQueue* queue);
bool queue_is_full(MessageQueue* queue);
int queue_count(MessageQueue* queue);

#endif // QUEUE_H
