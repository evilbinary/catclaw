#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "common/log.h"
#include "common/utils.h"
#include "message.h"
#include "common/cJSON.h"

const char* attachment_type_to_mime(const char* filename) {
    if (!filename) return "application/octet-stream";
    
    const char* ext = strrchr(filename, '.');
    if (!ext) return "application/octet-stream";
    
    if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcasecmp(ext, ".png") == 0) return "image/png";
    if (strcasecmp(ext, ".gif") == 0) return "image/gif";
    if (strcasecmp(ext, ".webp") == 0) return "image/webp";
    if (strcasecmp(ext, ".bmp") == 0) return "image/bmp";
    if (strcasecmp(ext, ".mp3") == 0) return "audio/mpeg";
    if (strcasecmp(ext, ".wav") == 0) return "audio/wav";
    if (strcasecmp(ext, ".ogg") == 0) return "audio/ogg";
    if (strcasecmp(ext, ".mp4") == 0) return "video/mp4";
    if (strcasecmp(ext, ".webm") == 0) return "video/webm";
    if (strcasecmp(ext, ".pdf") == 0) return "application/pdf";
    
    return "application/octet-stream";
}

char* message_file_to_base64(const char* file_path) {
    if (!file_path) return NULL;
    
    FILE* fp = fopen(file_path, "rb");
    if (!fp) {
        log_error("Failed to open file: %s", file_path);
        return NULL;
    }
    
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    if (file_size <= 0 || file_size > 20 * 1024 * 1024) {
        fclose(fp);
        log_error("Invalid file size: %ld", file_size);
        return NULL;
    }
    
    unsigned char* buffer = (unsigned char*)malloc(file_size);
    if (!buffer) {
        fclose(fp);
        return NULL;
    }
    
    size_t read_size = fread(buffer, 1, file_size, fp);
    fclose(fp);
    
    char* base64 = base64_encode(buffer, read_size);
    free(buffer);
    
    return base64;
}

Attachment* attachment_create(AttachmentType type, const char* data, const char* mime_type, const char* filename) {
    Attachment* att = (Attachment*)malloc(sizeof(Attachment));
    if (!att) return NULL;
    
    att->type = type;
    att->data = data ? strdup(data) : NULL;
    att->mime_type = mime_type ? strdup(mime_type) : NULL;
    att->filename = filename ? strdup(filename) : NULL;
    
    return att;
}

void attachment_destroy(Attachment* attachment) {
    if (attachment) {
        if (attachment->data) free(attachment->data);
        if (attachment->mime_type) free(attachment->mime_type);
        if (attachment->filename) free(attachment->filename);
        free(attachment);
    }
}

bool message_add_attachment(Message* message, Attachment* attachment) {
    if (!message || !attachment) return false;
    
    Attachment** new_attachments = (Attachment**)realloc(message->attachments, 
        sizeof(Attachment*) * (message->attachment_count + 1));
    if (!new_attachments) return false;
    
    message->attachments = new_attachments;
    message->attachments[message->attachment_count] = attachment;
    message->attachment_count++;
    
    return true;
}

bool message_add_attachment_file(Message* message, const char* file_path, AttachmentType type) {
    if (!message || !file_path) return false;
    
    char* base64 = message_file_to_base64(file_path);
    if (!base64) return false;
    
    const char* filename = strrchr(file_path, '/');
    if (!filename) filename = strrchr(file_path, '\\');
    filename = filename ? filename + 1 : file_path;
    
    const char* mime = attachment_type_to_mime(filename);
    
    Attachment* att = attachment_create(type, base64, mime, filename);
    free(base64);
    
    if (!att) return false;
    
    return message_add_attachment(message, att);
}

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
    message->attachments = NULL;
    message->attachment_count = 0;
    message->timestamp = time(NULL) * 1000;
    
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
    message->attachments = NULL;
    message->attachment_count = 0;
    message->timestamp = time(NULL) * 1000;
    
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
        if (message->attachments) {
            for (int i = 0; i < message->attachment_count; i++) {
                attachment_destroy(message->attachments[i]);
            }
            free(message->attachments);
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
    
    // Attachments
    if (message->attachments && message->attachment_count > 0) {
        cJSON* attachments_array = cJSON_CreateArray();
        for (int i = 0; i < message->attachment_count; i++) {
            Attachment* att = message->attachments[i];
            cJSON* att_obj = cJSON_CreateObject();
            cJSON_AddNumberToObject(att_obj, "type", att->type);
            if (att->data) cJSON_AddStringToObject(att_obj, "data", att->data);
            if (att->mime_type) cJSON_AddStringToObject(att_obj, "mime_type", att->mime_type);
            if (att->filename) cJSON_AddStringToObject(att_obj, "filename", att->filename);
            cJSON_AddItemToArray(attachments_array, att_obj);
        }
        cJSON_AddItemToObject(root, "attachments", attachments_array);
    }
    
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
    
    // Attachments
    cJSON* attachments_obj = cJSON_GetObjectItem(root, "attachments");
    if (attachments_obj && cJSON_IsArray(attachments_obj)) {
        int att_count = cJSON_GetArraySize(attachments_obj);
        for (int i = 0; i < att_count; i++) {
            cJSON* att_json = cJSON_GetArrayItem(attachments_obj, i);
            if (!att_json || !cJSON_IsObject(att_json)) continue;
            
            cJSON* type_obj = cJSON_GetObjectItem(att_json, "type");
            cJSON* data_obj = cJSON_GetObjectItem(att_json, "data");
            cJSON* mime_obj = cJSON_GetObjectItem(att_json, "mime_type");
            cJSON* filename_obj = cJSON_GetObjectItem(att_json, "filename");
            
            AttachmentType type = type_obj && cJSON_IsNumber(type_obj) ? (AttachmentType)type_obj->valueint : ATTACHMENT_FILE;
            const char* data = data_obj && cJSON_IsString(data_obj) ? data_obj->valuestring : NULL;
            const char* mime = mime_obj && cJSON_IsString(mime_obj) ? mime_obj->valuestring : NULL;
            const char* filename = filename_obj && cJSON_IsString(filename_obj) ? filename_obj->valuestring : NULL;
            
            Attachment* att = attachment_create(type, data, mime, filename);
            if (att) {
                message_add_attachment(message, att);
            }
        }
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
