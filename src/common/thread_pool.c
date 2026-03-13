#include "thread_pool.h"
#include "common/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Thread function
static void *thread_function(void *arg) {
    ThreadPool *pool = (ThreadPool *)arg;
    
    while (1) {
        pthread_mutex_lock(&pool->queue_mutex);
        
        // Wait until queue is not empty or shutdown
        while (pool->queue_count == 0 && !pool->shutdown) {
            pthread_cond_wait(&pool->queue_not_empty, &pool->queue_mutex);
        }
        
        // Check if shutdown
        if (pool->shutdown && pool->queue_count == 0) {
            pthread_mutex_unlock(&pool->queue_mutex);
            break;
        }
        
        // Get task from queue
        TaskFunction task = pool->task_functions[pool->queue_head];
        void *task_arg = pool->task_queue[pool->queue_head];
        
        // Update queue head
        pool->queue_head = (pool->queue_head + 1) % pool->queue_size;
        pool->queue_count--;
        
        // Signal that queue is not full
        pthread_cond_signal(&pool->queue_not_full);
        pthread_mutex_unlock(&pool->queue_mutex);
        
        // Execute task
        task(task_arg);
    }
    
    return NULL;
}

// Create thread pool
ThreadPool *thread_pool_create(int thread_count, int queue_size) {
    if (thread_count <= 0 || queue_size <= 0) {
        log_error("Invalid thread pool parameters");
        return NULL;
    }
    
    ThreadPool *pool = (ThreadPool *)malloc(sizeof(ThreadPool));
    if (!pool) {
        log_error("Failed to allocate thread pool");
        return NULL;
    }
    
    // Initialize pool
    pool->thread_count = thread_count;
    pool->queue_size = queue_size;
    pool->queue_head = 0;
    pool->queue_tail = 0;
    pool->queue_count = 0;
    pool->shutdown = false;
    
    // Allocate threads and queue
    pool->threads = (pthread_t *)malloc(sizeof(pthread_t) * thread_count);
    pool->task_queue = (void **)malloc(sizeof(void *) * queue_size);
    pool->task_functions = (TaskFunction *)malloc(sizeof(TaskFunction) * queue_size);
    
    if (!pool->threads || !pool->task_queue || !pool->task_functions) {
        log_error("Failed to allocate thread pool resources");
        free(pool->threads);
        free(pool->task_queue);
        free(pool->task_functions);
        free(pool);
        return NULL;
    }
    
    // Initialize mutex and condition variables
    if (pthread_mutex_init(&pool->queue_mutex, NULL) != 0) {
        log_error("Failed to initialize mutex");
        free(pool->threads);
        free(pool->task_queue);
        free(pool->task_functions);
        free(pool);
        return NULL;
    }
    
    if (pthread_cond_init(&pool->queue_not_empty, NULL) != 0) {
        log_error("Failed to initialize condition variable");
        pthread_mutex_destroy(&pool->queue_mutex);
        free(pool->threads);
        free(pool->task_queue);
        free(pool->task_functions);
        free(pool);
        return NULL;
    }
    
    if (pthread_cond_init(&pool->queue_not_full, NULL) != 0) {
        log_error("Failed to initialize condition variable");
        pthread_mutex_destroy(&pool->queue_mutex);
        pthread_cond_destroy(&pool->queue_not_empty);
        free(pool->threads);
        free(pool->task_queue);
        free(pool->task_functions);
        free(pool);
        return NULL;
    }
    
    // Create threads
    for (int i = 0; i < thread_count; i++) {
        if (pthread_create(&pool->threads[i], NULL, thread_function, pool) != 0) {
            log_error("Failed to create thread %d", i);
            // Cleanup already created threads
            pool->shutdown = true;
            pthread_cond_broadcast(&pool->queue_not_empty);
            for (int j = 0; j < i; j++) {
                pthread_join(pool->threads[j], NULL);
            }
            pthread_mutex_destroy(&pool->queue_mutex);
            pthread_cond_destroy(&pool->queue_not_empty);
            pthread_cond_destroy(&pool->queue_not_full);
            free(pool->threads);
            free(pool->task_queue);
            free(pool->task_functions);
            free(pool);
            return NULL;
        }
    }
    
    log_info("Thread pool created with %d threads and queue size %d", thread_count, queue_size);
    return pool;
}

// Destroy thread pool
void thread_pool_destroy(ThreadPool *pool) {
    if (!pool) {
        return;
    }
    
    // Set shutdown flag
    pthread_mutex_lock(&pool->queue_mutex);
    pool->shutdown = true;
    pthread_mutex_unlock(&pool->queue_mutex);
    
    // Wake up all threads
    pthread_cond_broadcast(&pool->queue_not_empty);
    
    // Join all threads
    for (int i = 0; i < pool->thread_count; i++) {
        pthread_join(pool->threads[i], NULL);
    }
    
    // Cleanup
    pthread_mutex_destroy(&pool->queue_mutex);
    pthread_cond_destroy(&pool->queue_not_empty);
    pthread_cond_destroy(&pool->queue_not_full);
    free(pool->threads);
    free(pool->task_queue);
    free(pool->task_functions);
    free(pool);
    
    log_info("Thread pool destroyed");
}

// Add task to thread pool
bool thread_pool_add_task(ThreadPool *pool, TaskFunction task, void *arg) {
    if (!pool || !task) {
        log_error("Invalid thread pool or task");
        return false;
    }
    
    pthread_mutex_lock(&pool->queue_mutex);
    
    // Wait until queue is not full
    while (pool->queue_count == pool->queue_size && !pool->shutdown) {
        pthread_cond_wait(&pool->queue_not_full, &pool->queue_mutex);
    }
    
    // Check if shutdown
    if (pool->shutdown) {
        pthread_mutex_unlock(&pool->queue_mutex);
        log_error("Thread pool is shutting down");
        return false;
    }
    
    // Add task to queue
    pool->task_functions[pool->queue_tail] = task;
    pool->task_queue[pool->queue_tail] = arg;
    pool->queue_tail = (pool->queue_tail + 1) % pool->queue_size;
    pool->queue_count++;
    
    // Signal that queue is not empty
    pthread_cond_signal(&pool->queue_not_empty);
    pthread_mutex_unlock(&pool->queue_mutex);
    
    return true;
}

// Get queue size
int thread_pool_get_queue_size(ThreadPool *pool) {
    if (!pool) {
        return -1;
    }
    
    pthread_mutex_lock(&pool->queue_mutex);
    int size = pool->queue_count;
    pthread_mutex_unlock(&pool->queue_mutex);
    
    return size;
}

// Get thread count
int thread_pool_get_thread_count(ThreadPool *pool) {
    if (!pool) {
        return -1;
    }
    
    return pool->thread_count;
}
