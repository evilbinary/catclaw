#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <stdbool.h>
#include <pthread.h>

// Task function type
typedef void (*TaskFunction)(void *arg);

// Thread pool structure
typedef struct {
    pthread_t *threads;
    int thread_count;
    void **task_queue;
    TaskFunction *task_functions;
    int queue_size;
    int queue_head;
    int queue_tail;
    int queue_count;
    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_not_empty;
    pthread_cond_t queue_not_full;
    bool shutdown;
} ThreadPool;

// Functions
ThreadPool *thread_pool_create(int thread_count, int queue_size);
void thread_pool_destroy(ThreadPool *pool);
bool thread_pool_add_task(ThreadPool *pool, TaskFunction task, void *arg);
int thread_pool_get_queue_size(ThreadPool *pool);
int thread_pool_get_thread_count(ThreadPool *pool);

#endif // THREAD_POOL_H
