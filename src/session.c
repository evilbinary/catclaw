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

#include "session.h"
#include "cJSON.h"

// Create a new session
Session* session_create(const char* session_key) {
    Session* session = (Session*)malloc(sizeof(Session));
    if (!session) {
        return NULL;
    }
    
    // Generate session ID from session key
    session->session_id = session_key_to_id(session_key);
    if (!session->session_id) {
        free(session);
        return NULL;
    }
    
    session->session_key = strdup(session_key);
    session->history = message_list_create();
    session->created_at = time(NULL) * 1000; // Milliseconds
    session->updated_at = session->created_at;
    session->compaction_count = 0;
    session->metadata = NULL;
    
    return session;
}

// Destroy a session
void session_destroy(Session* session) {
    if (session) {
        if (session->session_id) {
            free(session->session_id);
        }
        if (session->session_key) {
            free(session->session_key);
        }
        if (session->history) {
            message_list_destroy(session->history);
        }
        if (session->metadata) {
            free(session->metadata);
        }
        free(session);
    }
}

// Add a message to the session
bool session_add_message(Session* session, Message* message) {
    if (!session || !message) {
        return false;
    }
    
    if (!message_list_append(session->history, message)) {
        return false;
    }
    
    session->updated_at = time(NULL) * 1000; // Update timestamp
    return true;
}

// Save session to files
bool session_save(Session* session, const char* sessions_dir) {
    if (!session || !sessions_dir) {
        return false;
    }
    
    // Create sessions directory if it doesn't exist
    struct stat st;
    if (stat(sessions_dir, &st) != 0) {
        if (MKDIR(sessions_dir) != 0) {
            return false;
        }
    }
    
    // Save session history as JSONL
    char history_file[256];
    snprintf(history_file, sizeof(history_file), "%s/%s.jsonl", sessions_dir, session->session_id);
    
    char* jsonl = message_list_to_jsonl(session->history);
    if (!jsonl) {
        return false;
    }
    
    FILE* fp = fopen(history_file, "w");
    if (!fp) {
        free(jsonl);
        return false;
    }
    
    fprintf(fp, "%s", jsonl);
    fclose(fp);
    free(jsonl);
    
    // Save session metadata
    char metadata_file[256];
    snprintf(metadata_file, sizeof(metadata_file), "%s/sessions.json", sessions_dir);
    
    // Read existing metadata
    cJSON* metadata_root = NULL;
    FILE* metadata_fp = fopen(metadata_file, "r");
    if (metadata_fp) {
        char* metadata_content = NULL;
        fseek(metadata_fp, 0, SEEK_END);
        long length = ftell(metadata_fp);
        fseek(metadata_fp, 0, SEEK_SET);
        
        metadata_content = (char*)malloc(length + 1);
        if (metadata_content) {
            fread(metadata_content, 1, length, metadata_fp);
            metadata_content[length] = '\0';
            metadata_root = cJSON_Parse(metadata_content);
            free(metadata_content);
        }
        fclose(metadata_fp);
    }
    
    if (!metadata_root) {
        metadata_root = cJSON_CreateObject();
    }
    
    // Create session metadata object
    cJSON* session_metadata = cJSON_CreateObject();
    cJSON_AddStringToObject(session_metadata, "sessionId", session->session_id);
    cJSON_AddStringToObject(session_metadata, "sessionKey", session->session_key);
    cJSON_AddNumberToObject(session_metadata, "createdAt", session->created_at);
    cJSON_AddNumberToObject(session_metadata, "updatedAt", session->updated_at);
    cJSON_AddNumberToObject(session_metadata, "compactionCount", session->compaction_count);
    
    // Add to root
    cJSON_AddItemToObject(metadata_root, session->session_key, session_metadata);
    
    // Write back
    char* metadata_json = cJSON_PrintUnformatted(metadata_root);
    if (metadata_json) {
        metadata_fp = fopen(metadata_file, "w");
        if (metadata_fp) {
            fprintf(metadata_fp, "%s", metadata_json);
            fclose(metadata_fp);
        }
        free(metadata_json);
    }
    
    cJSON_Delete(metadata_root);
    return true;
}

// Load session from files
Session* session_load(const char* session_id, const char* sessions_dir) {
    if (!session_id || !sessions_dir) {
        return NULL;
    }
    
    // Read session history
    char history_file[256];
    snprintf(history_file, sizeof(history_file), "%s/%s.jsonl", sessions_dir, session_id);
    
    FILE* fp = fopen(history_file, "r");
    if (!fp) {
        return NULL;
    }
    
    // Read file content
    fseek(fp, 0, SEEK_END);
    long length = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    char* jsonl = (char*)malloc(length + 1);
    if (!jsonl) {
        fclose(fp);
        return NULL;
    }
    
    fread(jsonl, 1, length, fp);
    jsonl[length] = '\0';
    fclose(fp);
    
    // Parse JSONL
    MessageList* history = message_list_from_jsonl(jsonl);
    free(jsonl);
    if (!history) {
        return NULL;
    }
    
    // Read session metadata
    char metadata_file[256];
    snprintf(metadata_file, sizeof(metadata_file), "%s/sessions.json", sessions_dir);
    
    cJSON* metadata_root = NULL;
    FILE* metadata_fp = fopen(metadata_file, "r");
    if (metadata_fp) {
        char* metadata_content = NULL;
        fseek(metadata_fp, 0, SEEK_END);
        long metadata_length = ftell(metadata_fp);
        fseek(metadata_fp, 0, SEEK_SET);
        
        metadata_content = (char*)malloc(metadata_length + 1);
        if (metadata_content) {
            fread(metadata_content, 1, metadata_length, metadata_fp);
            metadata_content[metadata_length] = '\0';
            metadata_root = cJSON_Parse(metadata_content);
            free(metadata_content);
        }
        fclose(metadata_fp);
    }
    
    // Create session
    Session* session = (Session*)malloc(sizeof(Session));
    if (!session) {
        message_list_destroy(history);
        return NULL;
    }
    
    session->session_id = strdup(session_id);
    session->history = history;
    
    // Extract metadata
    if (metadata_root) {
        // Find session by ID (we need to iterate through all entries)
        cJSON* session_entry = NULL;
        cJSON* session_key = NULL;
        cJSON_ArrayForEach(session_entry, metadata_root) {
            cJSON* id_obj = cJSON_GetObjectItem(session_entry, "sessionId");
            if (id_obj && cJSON_IsString(id_obj) && strcmp(id_obj->valuestring, session_id) == 0) {
                session_key = cJSON_GetObjectItem(session_entry, "sessionKey");
                if (session_key && cJSON_IsString(session_key)) {
                    session->session_key = strdup(session_key->valuestring);
                }
                
                cJSON* created_at_obj = cJSON_GetObjectItem(session_entry, "createdAt");
                if (created_at_obj && cJSON_IsNumber(created_at_obj)) {
                    session->created_at = (long long)created_at_obj->valuedouble;
                }
                
                cJSON* updated_at_obj = cJSON_GetObjectItem(session_entry, "updatedAt");
                if (updated_at_obj && cJSON_IsNumber(updated_at_obj)) {
                    session->updated_at = (long long)updated_at_obj->valuedouble;
                }
                
                cJSON* compaction_count_obj = cJSON_GetObjectItem(session_entry, "compactionCount");
                if (compaction_count_obj && cJSON_IsNumber(compaction_count_obj)) {
                    session->compaction_count = (int)compaction_count_obj->valuedouble;
                }
                break;
            }
        }
        cJSON_Delete(metadata_root);
    }
    
    if (!session->session_key) {
        session->session_key = session_id_to_key(session_id);
    }
    
    session->compaction_count = 0;
    session->metadata = NULL;
    
    return session;
}

// Initialize session manager
SessionManager* session_manager_init(const char* sessions_dir, int max_sessions) {
    SessionManager* manager = (SessionManager*)malloc(sizeof(SessionManager));
    if (!manager) {
        return NULL;
    }
    
    manager->sessions_dir = strdup(sessions_dir);
    manager->max_sessions = max_sessions > 0 ? max_sessions : 100;
    manager->session_count = 0;
    manager->sessions = (Session**)malloc(sizeof(Session*) * manager->max_sessions);
    if (!manager->sessions) {
        free(manager->sessions_dir);
        free(manager);
        return NULL;
    }
    
    return manager;
}

// Destroy session manager
void session_manager_destroy(SessionManager* manager) {
    if (manager) {
        if (manager->sessions) {
            for (int i = 0; i < manager->session_count; i++) {
                session_destroy(manager->sessions[i]);
            }
            free(manager->sessions);
        }
        if (manager->sessions_dir) {
            free(manager->sessions_dir);
        }
        free(manager);
    }
}

// Get or create session
Session* session_manager_get_or_create(SessionManager* manager, const char* session_key) {
    if (!manager || !session_key) {
        return NULL;
    }
    
    // Check if session already exists
    for (int i = 0; i < manager->session_count; i++) {
        if (strcmp(manager->sessions[i]->session_key, session_key) == 0) {
            return manager->sessions[i];
        }
    }
    
    // Try to load from disk
    char* session_id = session_key_to_id(session_key);
    if (session_id) {
        Session* session = session_load(session_id, manager->sessions_dir);
        free(session_id);
        if (session) {
            // Add to manager
            if (manager->session_count >= manager->max_sessions) {
                // Remove oldest session
                session_destroy(manager->sessions[0]);
                for (int i = 0; i < manager->session_count - 1; i++) {
                    manager->sessions[i] = manager->sessions[i + 1];
                }
                manager->session_count--;
            }
            manager->sessions[manager->session_count++] = session;
            return session;
        }
    }
    
    // Create new session
    Session* session = session_create(session_key);
    if (session) {
        // Add to manager
        if (manager->session_count >= manager->max_sessions) {
            // Remove oldest session
            session_destroy(manager->sessions[0]);
            for (int i = 0; i < manager->session_count - 1; i++) {
                manager->sessions[i] = manager->sessions[i + 1];
            }
            manager->session_count--;
        }
        manager->sessions[manager->session_count++] = session;
    }
    
    return session;
}

// Get session by key
Session* session_manager_get(SessionManager* manager, const char* session_key) {
    if (!manager || !session_key) {
        return NULL;
    }
    
    for (int i = 0; i < manager->session_count; i++) {
        if (strcmp(manager->sessions[i]->session_key, session_key) == 0) {
            return manager->sessions[i];
        }
    }
    
    return NULL;
}

// Remove session
bool session_manager_remove(SessionManager* manager, const char* session_key) {
    if (!manager || !session_key) {
        return false;
    }
    
    for (int i = 0; i < manager->session_count; i++) {
        if (strcmp(manager->sessions[i]->session_key, session_key) == 0) {
            // Remove from array
            session_destroy(manager->sessions[i]);
            for (int j = i; j < manager->session_count - 1; j++) {
                manager->sessions[j] = manager->sessions[j + 1];
            }
            manager->session_count--;
            return true;
        }
    }
    
    return false;
}

// List all sessions
void session_manager_list(SessionManager* manager) {
    if (!manager) {
        return;
    }
    
    printf("Sessions:\n");
    for (int i = 0; i < manager->session_count; i++) {
        Session* session = manager->sessions[i];
        printf("  %s (ID: %s, Created: %lld, Messages: %d)\n", 
               session->session_key, 
               session->session_id, 
               session->created_at, 
               session->history->count);
    }
}

// Compact session
bool session_compact(Session* session, int threshold_tokens) {
    if (!session || threshold_tokens <= 0) {
        return false;
    }
    
    // Simple implementation: keep only the last 10 messages
    if (session->history->count > 10) {
        MessageList* new_history = message_list_create();
        if (!new_history) {
            return false;
        }
        
        // Keep last 10 messages
        int start = session->history->count - 10;
        for (int i = start; i < session->history->count; i++) {
            message_list_append(new_history, session->history->messages[i]);
        }
        
        // Replace history
        message_list_destroy(session->history);
        session->history = new_history;
        session->compaction_count++;
        session->updated_at = time(NULL) * 1000;
        
        return true;
    }
    
    return false;
}

// Convert session key to ID
char* session_key_to_id(const char* session_key) {
    if (!session_key) {
        return NULL;
    }
    
    // Replace colons with hyphens
    char* id = strdup(session_key);
    if (!id) {
        return NULL;
    }
    
    char* ptr = id;
    while (*ptr) {
        if (*ptr == ':') {
            *ptr = '-';
        }
        ptr++;
    }
    
    return id;
}

// Convert session ID to key
char* session_id_to_key(const char* session_id) {
    if (!session_id) {
        return NULL;
    }
    
    // Replace hyphens with colons
    char* key = strdup(session_id);
    if (!key) {
        return NULL;
    }
    
    char* ptr = key;
    while (*ptr) {
        if (*ptr == '-') {
            *ptr = ':';
        }
        ptr++;
    }
    
    return key;
}
