#ifndef MEMORY_H
#define MEMORY_H

#include <pthread.h>

// Memory entry structure
typedef struct {
    char* key;
    char* value;
    long long created_at;
    long long updated_at;
} MemoryEntry;

// Memory manager structure
typedef struct {
    MemoryEntry* entries;
    int count;
    int capacity;
    char* memory_dir;
    pthread_mutex_t mutex;
} MemoryManager;

// Functions
MemoryManager* memory_manager_init(const char* memory_dir);
void memory_manager_destroy(MemoryManager* manager);

bool memory_set(MemoryManager* manager, const char* key, const char* value);
char* memory_get(MemoryManager* manager, const char* key);
bool memory_delete(MemoryManager* manager, const char* key);
bool memory_clear(MemoryManager* manager);
void memory_list(MemoryManager* manager);

bool memory_save(MemoryManager* manager);
bool memory_load(MemoryManager* manager);

#endif // MEMORY_H
