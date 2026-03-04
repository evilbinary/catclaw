#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#else
#include <sys/types.h>
#define MKDIR(path) mkdir(path, 0755)
#endif

#include "memory.h"
#include "cJSON.h"

// Initialize memory manager
MemoryManager* memory_manager_init(const char* memory_dir) {
    MemoryManager* manager = (MemoryManager*)malloc(sizeof(MemoryManager));
    if (!manager) {
        return NULL;
    }
    
    manager->capacity = 100;
    manager->count = 0;
    manager->entries = (MemoryEntry*)malloc(sizeof(MemoryEntry) * manager->capacity);
    if (!manager->entries) {
        free(manager);
        return NULL;
    }
    
    manager->memory_dir = memory_dir ? strdup(memory_dir) : NULL;
    pthread_mutex_init(&manager->mutex, NULL);
    
    // Load memory from file if directory is provided
    if (manager->memory_dir) {
        memory_load(manager);
    }
    
    return manager;
}

// Destroy memory manager
void memory_manager_destroy(MemoryManager* manager) {
    if (manager) {
        if (manager->entries) {
            for (int i = 0; i < manager->count; i++) {
                if (manager->entries[i].key) {
                    free(manager->entries[i].key);
                }
                if (manager->entries[i].value) {
                    free(manager->entries[i].value);
                }
            }
            free(manager->entries);
        }
        if (manager->memory_dir) {
            free(manager->memory_dir);
        }
        pthread_mutex_destroy(&manager->mutex);
        free(manager);
    }
}

// Set memory entry
bool memory_set(MemoryManager* manager, const char* key, const char* value) {
    if (!manager || !key) {
        return false;
    }
    
    pthread_mutex_lock(&manager->mutex);
    
    // Check if key already exists
    int existing_index = -1;
    for (int i = 0; i < manager->count; i++) {
        if (strcmp(manager->entries[i].key, key) == 0) {
            existing_index = i;
            break;
        }
    }
    
    if (existing_index != -1) {
        // Update existing entry
        if (manager->entries[existing_index].value) {
            free(manager->entries[existing_index].value);
        }
        manager->entries[existing_index].value = value ? strdup(value) : NULL;
        manager->entries[existing_index].updated_at = time(NULL) * 1000;
    } else {
        // Add new entry
        if (manager->count >= manager->capacity) {
            // Resize
            int new_capacity = manager->capacity * 2;
            MemoryEntry* new_entries = (MemoryEntry*)realloc(manager->entries, sizeof(MemoryEntry) * new_capacity);
            if (!new_entries) {
                pthread_mutex_unlock(&manager->mutex);
                return false;
            }
            manager->entries = new_entries;
            manager->capacity = new_capacity;
        }
        
        manager->entries[manager->count].key = strdup(key);
        manager->entries[manager->count].value = value ? strdup(value) : NULL;
        manager->entries[manager->count].created_at = time(NULL) * 1000;
        manager->entries[manager->count].updated_at = manager->entries[manager->count].created_at;
        manager->count++;
    }
    
    // Save to file
    if (manager->memory_dir) {
        memory_save(manager);
    }
    
    pthread_mutex_unlock(&manager->mutex);
    return true;
}

// Get memory entry
char* memory_get(MemoryManager* manager, const char* key) {
    if (!manager || !key) {
        return NULL;
    }
    
    pthread_mutex_lock(&manager->mutex);
    
    for (int i = 0; i < manager->count; i++) {
        if (strcmp(manager->entries[i].key, key) == 0) {
            char* value = manager->entries[i].value ? strdup(manager->entries[i].value) : NULL;
            pthread_mutex_unlock(&manager->mutex);
            return value;
        }
    }
    
    pthread_mutex_unlock(&manager->mutex);
    return NULL;
}

// Delete memory entry
bool memory_delete(MemoryManager* manager, const char* key) {
    if (!manager || !key) {
        return false;
    }
    
    pthread_mutex_lock(&manager->mutex);
    
    int index = -1;
    for (int i = 0; i < manager->count; i++) {
        if (strcmp(manager->entries[i].key, key) == 0) {
            index = i;
            break;
        }
    }
    
    if (index != -1) {
        // Free entry
        if (manager->entries[index].key) {
            free(manager->entries[index].key);
        }
        if (manager->entries[index].value) {
            free(manager->entries[index].value);
        }
        
        // Shift entries
        for (int i = index; i < manager->count - 1; i++) {
            manager->entries[i] = manager->entries[i + 1];
        }
        manager->count--;
        
        // Save to file
        if (manager->memory_dir) {
            memory_save(manager);
        }
        
        pthread_mutex_unlock(&manager->mutex);
        return true;
    }
    
    pthread_mutex_unlock(&manager->mutex);
    return false;
}

// Clear all memory entries
bool memory_clear(MemoryManager* manager) {
    if (!manager) {
        return false;
    }
    
    pthread_mutex_lock(&manager->mutex);
    
    for (int i = 0; i < manager->count; i++) {
        if (manager->entries[i].key) {
            free(manager->entries[i].key);
        }
        if (manager->entries[i].value) {
            free(manager->entries[i].value);
        }
    }
    manager->count = 0;
    
    // Save to file
    if (manager->memory_dir) {
        memory_save(manager);
    }
    
    pthread_mutex_unlock(&manager->mutex);
    return true;
}

// List all memory entries
void memory_list(MemoryManager* manager) {
    if (!manager) {
        return;
    }
    
    pthread_mutex_lock(&manager->mutex);
    
    printf("Memory entries:\n");
    for (int i = 0; i < manager->count; i++) {
        printf("  %s: %s\n", manager->entries[i].key, manager->entries[i].value);
    }
    
    pthread_mutex_unlock(&manager->mutex);
}

// Save memory to file
bool memory_save(MemoryManager* manager) {
    if (!manager || !manager->memory_dir) {
        return false;
    }
    
    // Create memory directory if it doesn't exist
    struct stat st;
    if (stat(manager->memory_dir, &st) != 0) {
        if (MKDIR(manager->memory_dir) != 0) {
            return false;
        }
    }
    
    // Create memory file
    char memory_file[256];
    snprintf(memory_file, sizeof(memory_file), "%s/memory.json", manager->memory_dir);
    
    // Create JSON object
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return false;
    }
    
    // Add entries
    for (int i = 0; i < manager->count; i++) {
        MemoryEntry* entry = &manager->entries[i];
        cJSON* entry_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(entry_obj, "value", entry->value);
        cJSON_AddNumberToObject(entry_obj, "createdAt", entry->created_at);
        cJSON_AddNumberToObject(entry_obj, "updatedAt", entry->updated_at);
        cJSON_AddItemToObject(root, entry->key, entry_obj);
    }
    
    // Write to file
    char* json = cJSON_PrintUnformatted(root);
    if (json) {
        FILE* fp = fopen(memory_file, "w");
        if (fp) {
            fprintf(fp, "%s", json);
            fclose(fp);
        } else {
            free(json);
            cJSON_Delete(root);
            return false;
        }
        free(json);
    }
    
    cJSON_Delete(root);
    return true;
}

// Load memory from file
bool memory_load(MemoryManager* manager) {
    if (!manager || !manager->memory_dir) {
        return false;
    }
    
    // Create memory file path
    char memory_file[256];
    snprintf(memory_file, sizeof(memory_file), "%s/memory.json", manager->memory_dir);
    
    // Open file
    FILE* fp = fopen(memory_file, "r");
    if (!fp) {
        return false;
    }
    
    // Read file content
    fseek(fp, 0, SEEK_END);
    long length = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    char* content = (char*)malloc(length + 1);
    if (!content) {
        fclose(fp);
        return false;
    }
    
    fread(content, 1, length, fp);
    content[length] = '\0';
    fclose(fp);
    
    // Parse JSON
    cJSON* root = cJSON_Parse(content);
    free(content);
    
    if (!root) {
        return false;
    }
    
    // Clear existing entries
    for (int i = 0; i < manager->count; i++) {
        if (manager->entries[i].key) {
            free(manager->entries[i].key);
        }
        if (manager->entries[i].value) {
            free(manager->entries[i].value);
        }
    }
    manager->count = 0;
    
    // Add entries from JSON
    cJSON* entry_obj = NULL;
    cJSON_ArrayForEach(entry_obj, root) {
        const char* key = entry_obj->string;
        cJSON* value_obj = cJSON_GetObjectItem(entry_obj, "value");
        cJSON* created_at_obj = cJSON_GetObjectItem(entry_obj, "createdAt");
        cJSON* updated_at_obj = cJSON_GetObjectItem(entry_obj, "updatedAt");
        
        if (value_obj && cJSON_IsString(value_obj)) {
            // Resize if needed
            if (manager->count >= manager->capacity) {
                int new_capacity = manager->capacity * 2;
                MemoryEntry* new_entries = (MemoryEntry*)realloc(manager->entries, sizeof(MemoryEntry) * new_capacity);
                if (!new_entries) {
                    cJSON_Delete(root);
                    return false;
                }
                manager->entries = new_entries;
                manager->capacity = new_capacity;
            }
            
            manager->entries[manager->count].key = strdup(key);
            manager->entries[manager->count].value = strdup(value_obj->valuestring);
            manager->entries[manager->count].created_at = created_at_obj && cJSON_IsNumber(created_at_obj) ? (long long)created_at_obj->valuedouble : time(NULL) * 1000;
            manager->entries[manager->count].updated_at = updated_at_obj && cJSON_IsNumber(updated_at_obj) ? (long long)updated_at_obj->valuedouble : time(NULL) * 1000;
            manager->count++;
        }
    }
    
    cJSON_Delete(root);
    return true;
}
