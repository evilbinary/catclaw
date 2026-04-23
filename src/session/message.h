#ifndef MESSAGE_H
#define MESSAGE_H

#include <stdbool.h>

// Message role enumeration
typedef enum {
    ROLE_USER,
    ROLE_ASSISTANT,
    ROLE_SYSTEM,
    ROLE_TOOL
} MessageRole;

// Attachment type enumeration
typedef enum {
    ATTACHMENT_IMAGE,
    ATTACHMENT_AUDIO,
    ATTACHMENT_VIDEO,
    ATTACHMENT_PDF,
    ATTACHMENT_FILE
} AttachmentType;

// Attachment structure
typedef struct {
    AttachmentType type;
    char* data;          // base64 encoded data
    char* mime_type;     // MIME type (e.g., "image/jpeg", "audio/mp3")
    char* filename;      // original filename (optional)
} Attachment;

// Message structure
typedef struct {
    MessageRole role;
    char* content;
    char* tool_name;       // 工具名称（如果是工具消息）
    char* tool_call_id;    // 工具调用 ID
    Attachment** attachments;  // 附件数组
    int attachment_count;      // 附件数量
    long long timestamp;   // 时间戳
} Message;

// Message list structure
typedef struct {
    Message** messages;
    int count;
    int capacity;
} MessageList;

// Functions
Message* message_create(MessageRole role, const char* content);
Message* message_create_tool(const char* tool_call_id, const char* tool_name, const char* content);
void message_destroy(Message* message);

// Attachment functions
Attachment* attachment_create(AttachmentType type, const char* data, const char* mime_type, const char* filename);
void attachment_destroy(Attachment* attachment);
bool message_add_attachment(Message* message, Attachment* attachment);
bool message_add_attachment_file(Message* message, const char* file_path, AttachmentType type);
char* message_file_to_base64(const char* file_path);
const char* attachment_type_to_mime(const char* filename);

MessageList* message_list_create(void);
void message_list_destroy(MessageList* list);
bool message_list_append(MessageList* list, Message* message);
MessageList* message_list_slice(MessageList* list, int start, int end);
void message_list_clear(MessageList* list);

// JSON serialization/deserialization
char* message_to_json(const Message* message);
Message* message_from_json(const char* json);
char* message_list_to_jsonl(const MessageList* list);
MessageList* message_list_from_jsonl(const char* jsonl);

#endif // MESSAGE_H
