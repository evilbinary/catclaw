#ifndef SESSION_H
#define SESSION_H

#include "message.h"

// Session structure
typedef struct {
    char* session_id;       // 会话 ID
    char* session_key;      // 会话键
    MessageList* history;   // 消息历史
    long long created_at;   // 创建时间
    long long updated_at;   // 更新时间
    int compaction_count;   // 压缩次数
    char* metadata;         // JSON 格式的元数据
} Session;

// Session manager structure
typedef struct {
    char* sessions_dir;     // 会话存储目录
    int max_sessions;       // 最大会话数
    int session_count;      // 当前会话数
    Session** sessions;     // 会话数组
} SessionManager;

// Functions
Session* session_create(const char* session_key);
void session_destroy(Session* session);

bool session_add_message(Session* session, Message* message);
bool session_save(Session* session, const char* sessions_dir);
Session* session_load(const char* session_id, const char* sessions_dir);

SessionManager* session_manager_init(const char* sessions_dir, int max_sessions);
void session_manager_destroy(SessionManager* manager);

Session* session_manager_get_or_create(SessionManager* manager, const char* session_key);
Session* session_manager_get(SessionManager* manager, const char* session_key);
bool session_manager_remove(SessionManager* manager, const char* session_key);
void session_manager_list(SessionManager* manager);

// Session compaction
bool session_compact(Session* session, int threshold_tokens);

// Helper functions
char* session_key_to_id(const char* session_key);
char* session_id_to_key(const char* session_id);

#endif // SESSION_H
