#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <stdbool.h>

#include "queue.h"

// Create a queue item
QueueItem* queue_item_create(const char* session_key, Message* message, QueueMode mode) {
    QueueItem* item = (QueueItem*)malloc(sizeof(QueueItem));
    if (!item) {
        return NULL;
    }
    
    item->session_key = session_key ? strdup(session_key) : NULL;
    item->message = message;
    item->mode = mode;
    item->enqueue_time = time(NULL) * 1000; // Milliseconds
    
    return item;
}

// Destroy a queue item
void queue_item_destroy(QueueItem* item) {
    if (item) {
        if (item->session_key) {
            free(item->session_key);
        }
        if (item->message) {
            message_destroy(item->message);
        }
        free(item);
    }
}

// Initialize a message queue
MessageQueue* queue_init(int capacity) {
    if (capacity <= 0) {
        capacity = 100; // Default capacity
    }
    
    MessageQueue* queue = (MessageQueue*)malloc(sizeof(MessageQueue));
    if (!queue) {
        return NULL;
    }
    
    queue->items = (QueueItem**)malloc(sizeof(QueueItem*) * capacity);
    if (!queue->items) {
        free(queue);
        return NULL;
    }
    
    queue->front = 0;
    queue->rear = 0;
    queue->count = 0;
    queue->capacity = capacity;
    
    // Initialize mutex and condition variables
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->not_empty, NULL);
    pthread_cond_init(&queue->not_full, NULL);
    
    return queue;
}

// Destroy a message queue
void queue_destroy(MessageQueue* queue) {
    if (queue) {
        // Destroy all items
        while (queue->count > 0) {
            QueueItem* item = queue->items[queue->front];
            queue_item_destroy(item);
            queue->front = (queue->front + 1) % queue->capacity;
            queue->count--;
        }
        
        free(queue->items);
        
        // Destroy mutex and condition variables
        pthread_mutex_destroy(&queue->mutex);
        pthread_cond_destroy(&queue->not_empty);
        pthread_cond_destroy(&queue->not_full);
        
        free(queue);
    }
}

// Enqueue an item
bool queue_enqueue(MessageQueue* queue, QueueItem* item) {
    if (!queue || !item) {
        return false;
    }
    
    pthread_mutex_lock(&queue->mutex);
    
    // Wait if queue is full
    while (queue->count >= queue->capacity) {
        if (pthread_cond_wait(&queue->not_full, &queue->mutex) != 0) {
            pthread_mutex_unlock(&queue->mutex);
            return false;
        }
    }
    
    // Add item to queue
    queue->items[queue->rear] = item;
    queue->rear = (queue->rear + 1) % queue->capacity;
    queue->count++;
    
    // Signal that queue is not empty
    pthread_cond_signal(&queue->not_empty);
    
    pthread_mutex_unlock(&queue->mutex);
    return true;
}

// Dequeue an item with timeout
QueueItem* queue_dequeue(MessageQueue* queue, int timeout_ms) {
    if (!queue) {
        return NULL;
    }
    
    pthread_mutex_lock(&queue->mutex);
    
    // Wait if queue is empty
    if (queue->count == 0) {
        if (timeout_ms <= 0) {
            // Wait indefinitely
            if (pthread_cond_wait(&queue->not_empty, &queue->mutex) != 0) {
                pthread_mutex_unlock(&queue->mutex);
                return NULL;
            }
        } else {
            // Wait with timeout
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += timeout_ms / 1000;
            ts.tv_nsec += (timeout_ms % 1000) * 1000000;
            if (ts.tv_nsec >= 1000000000) {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000;
            }
            
            int result = pthread_cond_timedwait(&queue->not_empty, &queue->mutex, &ts);
            if (result == ETIMEDOUT) {
                pthread_mutex_unlock(&queue->mutex);
                return NULL;
            } else if (result != 0) {
                pthread_mutex_unlock(&queue->mutex);
                return NULL;
            }
        }
    }
    
    // Remove item from queue
    QueueItem* item = queue->items[queue->front];
    queue->front = (queue->front + 1) % queue->capacity;
    queue->count--;
    
    // Signal that queue is not full
    pthread_cond_signal(&queue->not_full);
    
    pthread_mutex_unlock(&queue->mutex);
    return item;
}

// Check if queue is empty
bool queue_is_empty(MessageQueue* queue) {
    if (!queue) {
        return true;
    }
    
    pthread_mutex_lock(&queue->mutex);
    bool empty = (queue->count == 0);
    pthread_mutex_unlock(&queue->mutex);
    
    return empty;
}

// Check if queue is full
bool queue_is_full(MessageQueue* queue) {
    if (!queue) {
        return true;
    }
    
    pthread_mutex_lock(&queue->mutex);
    bool full = (queue->count >= queue->capacity);
    pthread_mutex_unlock(&queue->mutex);
    
    return full;
}

// Get queue count
int queue_count(MessageQueue* queue) {
    if (!queue) {
        return 0;
    }
    
    pthread_mutex_lock(&queue->mutex);
    int count = queue->count;
    pthread_mutex_unlock(&queue->mutex);
    
    return count;
}
