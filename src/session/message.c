#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "common/log.h"
#include "message.h"
#include "common/cJSON.h"

// Create a new message
Message* message_create(MessageRole role, const char* content) {
    Message* message = (Message*)malloc(sizeof(Message));
    if (!message) {
        return NULL;
    }
    
    message->role = role;
    message->content = content ? strdup(content) : NULL;
    message->tool_name = NULL;
    message->tool_call_id = NULL;
    message->timestamp = time(NULL) * 1000; // Milliseconds
    
    return message;
}

// Create a tool message
Message* message_create_tool(const char* tool_call_id, const char* tool_name, const char* content) {
    Message* message = (Message*)malloc(sizeof(Message));
    if (!message) {
        return NULL;
    }
    
    message->role = ROLE_TOOL;
    message->content = content ? strdup(content) : NULL;
    message->tool_name = tool_name ? strdup(tool_name) : NULL;
    message->tool_call_id = tool_call_id ? strdup(tool_call_id) : NULL;
    message->timestamp = time(NULL) * 1000; // Milliseconds
    
    return message;
}

// Destroy a message
void message_destroy(Message* message) {
    if (message) {
        if (message->content) {
            free(message->content);
        }
        if (message->tool_name) {
            free(message->tool_name);
        }
        if (message->tool_call_id) {
            free(message->tool_call_id);
        }
        free(message);
    }
}

// Create a message list
MessageList* message_list_create(void) {
    MessageList* list = (MessageList*)malloc(sizeof(MessageList));
    if (!list) {
        return NULL;
    }
    
    list->capacity = 10;
    list->count = 0;
    list->messages = (Message**)malloc(sizeof(Message*) * list->capacity);
    if (!list->messages) {
        free(list);
        return NULL;
    }
    
    // Initialize all message pointers to NULL
    for (int i = 0; i < list->capacity; i++) {
        list->messages[i] = NULL;
    }
    
    return list;
}

// Destroy a message list (without destroying the messages themselves)
void message_list_destroy(MessageList* list) {
    if (list) {
        if (list->messages) {
            free(list->messages);
        }
        free(list);
    }
}

// Append a message to the list
bool message_list_append(MessageList* list, Message* message) {
    if (!list || !message) {
        return false;
    }
    
    // Resize if needed
    if (list->count >= list->capacity) {
        int new_capacity = list->capacity * 2;
        Message** new_messages = (Message**)realloc(list->messages, sizeof(Message*) * new_capacity);
        if (!new_messages) {
            return false;
        }
        
        // Initialize new memory to NULL
        for (int i = list->capacity; i < new_capacity; i++) {
            new_messages[i] = NULL;
        }
        
        list->messages = new_messages;
        list->capacity = new_capacity;
    }
    
    list->messages[list->count++] = message;
    return true;
}

// Slice a message list
MessageList* message_list_slice(MessageList* list, int start, int end) {
    if (!list || start < 0 || end > list->count || start >= end) {
        return NULL;
    }
    
    MessageList* slice = message_list_create();
    if (!slice) {
        return NULL;
    }
    
    for (int i = start; i < end; i++) {
        if (!message_list_append(slice, list->messages[i])) {
            message_list_destroy(slice);
            return NULL;
        }
    }
    
    return slice;
}

// Clear a message list
void message_list_clear(MessageList* list) {
    if (list) {
        for (int i = 0; i < list->count; i++) {
            message_destroy(list->messages[i]);
        }
        list->count = 0;
    }
}

// Convert message to JSON
char* message_to_json(const Message* message) {
    if (!message) {
        log_debug("message_to_json: message is NULL");
        return NULL;
    }
    
    // log_debug("message_to_json: processing message with role %d", message->role);
    
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        log_debug("message_to_json: cJSON_CreateObject failed");
        return NULL;
    }
    
    // Role
    const char* role_str;
    switch (message->role) {
        case ROLE_USER:
            role_str = "user";
            break;
        case ROLE_ASSISTANT:
            role_str = "assistant";
            break;
        case ROLE_SYSTEM:
            role_str = "system";
            break;
        case ROLE_TOOL:
            role_str = "tool";
            break;
        default:
            role_str = "user";
            break;
    }
    //log_debug("message_to_json: adding role: %s", role_str);
    cJSON_AddStringToObject(root, "role", role_str);
    
    // Content (use empty string if NULL)
    const char* content_str = message->content ? message->content : "";
    // log_debug("message_to_json: adding content: %s", content_str);
    cJSON_AddStringToObject(root, "content", content_str);
    
    // Tool name (for tool messages)
    if (message->tool_name) {
        // log_debug("message_to_json: adding tool_name: %s", message->tool_name);
        cJSON_AddStringToObject(root, "tool_name", message->tool_name);
    }
    
    // Tool call ID (for tool messages)
    if (message->tool_call_id) {
        // log_debug("message_to_json: adding tool_call_id: %s", message->tool_call_id);
        cJSON_AddStringToObject(root, "tool_call_id", message->tool_call_id);
    }
    
    // Timestamp
    // log_debug("message_to_json: adding timestamp: %lld", message->timestamp);
    cJSON_AddNumberToObject(root, "timestamp", message->timestamp);
    
    // log_debug("message_to_json: calling cJSON_PrintUnformatted");
    char* json = cJSON_PrintUnformatted(root);
    // log_debug("message_to_json: cJSON_PrintUnformatted returned");
    cJSON_Delete(root);
    
    return json;
}

// Convert JSON to message
Message* message_from_json(const char* json) {
    if (!json) {
        return NULL;
    }
    
    cJSON* root = cJSON_Parse(json);
    if (!root) {
        return NULL;
    }
    
    // Role
    cJSON* role_obj = cJSON_GetObjectItem(root, "role");
    if (!role_obj || !cJSON_IsString(role_obj)) {
        cJSON_Delete(root);
        return NULL;
    }
    
    MessageRole role;
    const char* role_str = role_obj->valuestring;
    if (strcmp(role_str, "user") == 0) {
        role = ROLE_USER;
    } else if (strcmp(role_str, "assistant") == 0) {
        role = ROLE_ASSISTANT;
    } else if (strcmp(role_str, "system") == 0) {
        role = ROLE_SYSTEM;
    } else if (strcmp(role_str, "tool") == 0) {
        role = ROLE_TOOL;
    } else {
        cJSON_Delete(root);
        return NULL;
    }
    
    // Content
    cJSON* content_obj = cJSON_GetObjectItem(root, "content");
    const char* content = content_obj && cJSON_IsString(content_obj) ? content_obj->valuestring : NULL;
    
    Message* message;
    if (role == ROLE_TOOL) {
        // Tool message
        cJSON* tool_name_obj = cJSON_GetObjectItem(root, "tool_name");
        const char* tool_name = tool_name_obj && cJSON_IsString(tool_name_obj) ? tool_name_obj->valuestring : NULL;
        
        cJSON* tool_call_id_obj = cJSON_GetObjectItem(root, "tool_call_id");
        const char* tool_call_id = tool_call_id_obj && cJSON_IsString(tool_call_id_obj) ? tool_call_id_obj->valuestring : NULL;
        
        message = message_create_tool(tool_call_id, tool_name, content);
    } else {
        // Regular message
        message = message_create(role, content);
    }
    
    // Timestamp
    cJSON* timestamp_obj = cJSON_GetObjectItem(root, "timestamp");
    if (timestamp_obj && cJSON_IsNumber(timestamp_obj)) {
        message->timestamp = (long long)timestamp_obj->valuedouble;
    }
    
    cJSON_Delete(root);
    return message;
}

// Convert message list to JSONL
char* message_list_to_jsonl(const MessageList* list) {
    if (!list) {
        return NULL;
    }
    
    // Use a dynamic buffer to avoid length calculation issues with UTF-8
    char* jsonl = NULL;
    size_t current_size = 0;
    size_t position = 0;
    
    // Build JSONL
    for (int i = 0; i < list->count; i++) {
        if (!list->messages[i]) continue;  // Skip NULL messages
        
        char* json = message_to_json(list->messages[i]);
        if (json) {
            size_t json_len = strlen(json);
            size_t needed = position + json_len + 2; // +2 for newline and null terminator
            
            // Reallocate buffer if needed
            if (needed > current_size) {
                size_t new_size = current_size ? current_size * 2 : 1024;
                while (new_size < needed) {
                    new_size *= 2;
                }
                
                char* new_buffer = (char*)realloc(jsonl, new_size);
                if (!new_buffer) {
                    free(json);
                    if (jsonl) free(jsonl);
                    return NULL;
                }
                jsonl = new_buffer;
                current_size = new_size;
            }
            
            // Copy JSON and add newline
            strcpy(jsonl + position, json);
            position += json_len;
            jsonl[position++] = '\n';
            
            free(json);
        }
    }
    
    // Add null terminator
    if (jsonl) {
        if (position < current_size) {
            jsonl[position] = '\0';
        } else {
            // Just in case we need one more byte
            char* new_buffer = (char*)realloc(jsonl, current_size + 1);
            if (new_buffer) {
                new_buffer[position] = '\0';
                jsonl = new_buffer;
            } else {
                free(jsonl);
                return NULL;
            }
        }
    }
    
    return jsonl;
}

// Convert JSONL to message list
MessageList* message_list_from_jsonl(const char* jsonl) {
    if (!jsonl) {
        return NULL;
    }
    
    MessageList* list = message_list_create();
    if (!list) {
        return NULL;
    }
    
    // Parse line by line
    char* line = strdup(jsonl);
    if (!line) {
        message_list_destroy(list);
        return NULL;
    }
    
    char* token = strtok(line, "\n");
    while (token) {
        Message* message = message_from_json(token);
        if (message) {
            message_list_append(list, message);
        }
        token = strtok(NULL, "\n");
    }
    
    free(line);
    return list;
}
