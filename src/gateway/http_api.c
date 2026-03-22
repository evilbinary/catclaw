#include "http_api.h"
#include "http_server.h"
#include "../common/config.h"
#include "../common/log.h"
#include "../common/cJSON.h"
#include "../agent/agent.h"
#include "../model/ai_model.h"
#include "../tool/tool.h"
#include "../session/session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ==================== JSON 解析辅助 ====================

ChatRequest* chat_request_parse(const char* json) {
    if (!json) return NULL;
    
    cJSON* root = cJSON_Parse(json);
    if (!root) {
        log_error("Failed to parse chat request JSON");
        return NULL;
    }
    
    ChatRequest* req = (ChatRequest*)calloc(1, sizeof(ChatRequest));
    if (!req) {
        cJSON_Delete(root);
        return NULL;
    }
    
    cJSON* message = cJSON_GetObjectItem(root, "message");
    if (message && cJSON_IsString(message)) {
        req->message = strdup(message->valuestring);
    }
    
    cJSON* session_id = cJSON_GetObjectItem(root, "session_id");
    if (session_id && cJSON_IsString(session_id)) {
        req->session_id = strdup(session_id->valuestring);
    } else {
        req->session_id = strdup("default");
    }
    
    cJSON* stream = cJSON_GetObjectItem(root, "stream");
    req->stream = stream && cJSON_IsBool(stream) ? cJSON_IsTrue(stream) : false;
    
    cJSON_Delete(root);
    return req;
}

void chat_request_free(ChatRequest* req) {
    if (!req) return;
    free(req->message);
    free(req->session_id);
    free(req);
}

ModelSwitchRequest* model_switch_request_parse(const char* json) {
    if (!json) return NULL;
    
    cJSON* root = cJSON_Parse(json);
    if (!root) return NULL;
    
    ModelSwitchRequest* req = (ModelSwitchRequest*)calloc(1, sizeof(ModelSwitchRequest));
    if (!req) {
        cJSON_Delete(root);
        return NULL;
    }
    
    cJSON* model = cJSON_GetObjectItem(root, "model");
    if (model && cJSON_IsString(model)) {
        req->model = strdup(model->valuestring);
    }
    
    cJSON_Delete(root);
    return req;
}

void model_switch_request_free(ModelSwitchRequest* req) {
    if (!req) return;
    free(req->model);
    free(req);
}

ToolExecuteRequest* tool_execute_request_parse(const char* json) {
    if (!json) return NULL;
    
    cJSON* root = cJSON_Parse(json);
    if (!root) return NULL;
    
    ToolExecuteRequest* req = (ToolExecuteRequest*)calloc(1, sizeof(ToolExecuteRequest));
    if (!req) {
        cJSON_Delete(root);
        return NULL;
    }
    
    cJSON* name = cJSON_GetObjectItem(root, "name");
    if (name && cJSON_IsString(name)) {
        req->name = strdup(name->valuestring);
    }
    
    cJSON* args = cJSON_GetObjectItem(root, "args");
    if (args) {
        req->args = cJSON_Print(args);
    }
    
    cJSON_Delete(root);
    return req;
}

void tool_execute_request_free(ToolExecuteRequest* req) {
    if (!req) return;
    free(req->name);
    free(req->args);
    free(req);
}

// ==================== API 处理函数 ====================

// POST /chat
static HttpResponse* handle_chat(const HttpRequest* request, void* user_data) {
    (void)user_data;
    
    if (!request->body) {
        return http_response_error(400, "Missing request body");
    }
    
    ChatRequest* chat_req = chat_request_parse(request->body);
    if (!chat_req || !chat_req->message) {
        chat_request_free(chat_req);
        return http_response_error(400, "Invalid chat request");
    }
    
    // 发送消息到 agent
    if (!agent_send_message(chat_req->message)) {
        chat_request_free(chat_req);
        return http_response_error(500, "Failed to send message");
    }
    
    // 构造响应
    cJSON* response_json = cJSON_CreateObject();
    cJSON_AddStringToObject(response_json, "status", "queued");
    cJSON_AddStringToObject(response_json, "session_id", chat_req->session_id ? chat_req->session_id : "default");
    cJSON_AddStringToObject(response_json, "message", "Message sent successfully");
    
    char* json_str = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    HttpResponse* response = http_response_json(200, json_str);
    free(json_str);
    chat_request_free(chat_req);
    
    return response;
}

// 流式 chat 处理
static bool handle_chat_stream(const HttpRequest* request, 
                                StreamCallback callback, 
                                void* user_data) {
    (void)user_data;
    
    if (!request->body) {
        char* err = "{\"error\":\"Missing request body\"}";
        callback(err, strlen(err), user_data);
        return false;
    }
    
    ChatRequest* chat_req = chat_request_parse(request->body);
    if (!chat_req || !chat_req->message) {
        char* err = "{\"error\":\"Invalid chat request\"}";
        callback(err, strlen(err), user_data);
        chat_request_free(chat_req);
        return false;
    }
    
    // TODO: 实现真正的流式响应
    // 目前发送消息后返回状态
    char* status1 = "event: status\ndata: Message processing...\n\n";
    callback(status1, strlen(status1), user_data);
    
    // 发送消息
    agent_send_message(chat_req->message);
    
    char* status2 = "event: status\ndata: Message queued\n\n";
    callback(status2, strlen(status2), user_data);
    
    char* done = "event: done\ndata: [DONE]\n\n";
    callback(done, strlen(done), user_data);
    
    chat_request_free(chat_req);
    return true;
}

// GET /health
static HttpResponse* handle_health(const HttpRequest* request, void* user_data) {
    (void)request;
    (void)user_data;
    
    cJSON* response_json = cJSON_CreateObject();
    cJSON_AddStringToObject(response_json, "status", "healthy");
    cJSON_AddStringToObject(response_json, "version", "1.0.0");
    cJSON_AddStringToObject(response_json, "model", g_agent.model ? g_agent.model : "unknown");
    
    char* json_str = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    HttpResponse* response = http_response_json(200, json_str);
    free(json_str);
    
    return response;
}

// GET /models
static HttpResponse* handle_models(const HttpRequest* request, void* user_data) {
    (void)request;
    (void)user_data;
    
    cJSON* response_json = cJSON_CreateObject();
    cJSON* models = cJSON_CreateArray();
    
    cJSON_AddItemToArray(models, cJSON_CreateString("openai/gpt-4o"));
    cJSON_AddItemToArray(models, cJSON_CreateString("openai/gpt-3.5-turbo"));
    cJSON_AddItemToArray(models, cJSON_CreateString("anthropic/claude-3-opus-20240229"));
    cJSON_AddItemToArray(models, cJSON_CreateString("anthropic/claude-3-sonnet-20240229"));
    cJSON_AddItemToArray(models, cJSON_CreateString("gemini/gemini-1.5-pro"));
    cJSON_AddItemToArray(models, cJSON_CreateString("llama/llama3-70b"));
    
    cJSON_AddItemToObject(response_json, "models", models);
    cJSON_AddStringToObject(response_json, "current", g_agent.model ? g_agent.model : "unknown");
    
    char* json_str = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    HttpResponse* response = http_response_json(200, json_str);
    free(json_str);
    
    return response;
}

// POST /model/switch
static HttpResponse* handle_model_switch(const HttpRequest* request, void* user_data) {
    (void)user_data;
    
    if (!request->body) {
        return http_response_error(400, "Missing request body");
    }
    
    ModelSwitchRequest* switch_req = model_switch_request_parse(request->body);
    if (!switch_req || !switch_req->model) {
        model_switch_request_free(switch_req);
        return http_response_error(400, "Invalid model switch request");
    }
    
    // 切换模型
    bool success = config_switch_model(switch_req->model);
    
    cJSON* response_json = cJSON_CreateObject();
    
    if (success) {
        // 更新 AI model 配置
        AIModelConfig model_config = {0};
        model_config.model_name = g_config.model.model_name;
        model_config.api_key = g_config.model.api_key;
        model_config.base_url = g_config.model.base_url;
        model_config.temperature = g_config.model.temperature;
        model_config.max_tokens = g_config.model.max_tokens;
        model_config.stream = g_config.model.stream;
        
        if (ai_model_set_config(&model_config)) {
            if (g_agent.model) free(g_agent.model);
            g_agent.model = strdup(g_config.model.name);
            
            cJSON_AddStringToObject(response_json, "status", "success");
            cJSON_AddStringToObject(response_json, "model", g_config.model.name);
            cJSON_AddStringToObject(response_json, "message", "Model switched successfully");
        } else {
            cJSON_AddStringToObject(response_json, "status", "error");
            cJSON_AddStringToObject(response_json, "error", "Failed to update AI model config");
        }
    } else {
        cJSON_AddStringToObject(response_json, "status", "error");
        cJSON_AddStringToObject(response_json, "error", "Failed to switch model");
    }
    
    char* json_str = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    HttpResponse* response = http_response_json(success ? 200 : 400, json_str);
    free(json_str);
    model_switch_request_free(switch_req);
    
    return response;
}

// GET /tools
static HttpResponse* handle_tools(const HttpRequest* request, void* user_data) {
    (void)request;
    (void)user_data;
    
    cJSON* response_json = cJSON_CreateObject();
    cJSON* tools = cJSON_CreateArray();
    
    // 遍历工具注册表
    if (g_agent.tool_registry) {
        for (int i = 0; i < g_agent.tool_registry->count; i++) {
            Tool* tool = g_agent.tool_registry->tools[i];
            if (tool) {
                cJSON* tool_obj = cJSON_CreateObject();
                cJSON_AddStringToObject(tool_obj, "name", tool->name);
                cJSON_AddStringToObject(tool_obj, "description", tool->description ? tool->description : "");
                cJSON_AddItemToArray(tools, tool_obj);
            }
        }
    }
    
    cJSON_AddItemToObject(response_json, "tools", tools);
    
    char* json_str = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    HttpResponse* response = http_response_json(200, json_str);
    free(json_str);
    
    return response;
}

// POST /tools/execute
static HttpResponse* handle_tools_execute(const HttpRequest* request, void* user_data) {
    (void)user_data;
    
    if (!request->body) {
        return http_response_error(400, "Missing request body");
    }
    
    ToolExecuteRequest* tool_req = tool_execute_request_parse(request->body);
    if (!tool_req || !tool_req->name) {
        tool_execute_request_free(tool_req);
        return http_response_error(400, "Invalid tool execute request");
    }
    
    // 执行工具
    char* result = NULL;
    int result_len = 0;
    
    ToolArgs* args = tool_args_from_string(tool_req->args ? tool_req->args : "");
    int ret = agent_execute_tool(tool_req->name, args, &result, &result_len);
    tool_args_free(args);
    
    cJSON* response_json = cJSON_CreateObject();
    cJSON_AddStringToObject(response_json, "tool", tool_req->name);
    
    if (ret == 0 && result) {
        cJSON_AddStringToObject(response_json, "status", "success");
        cJSON_AddStringToObject(response_json, "result", result);
        free(result);
    } else {
        cJSON_AddStringToObject(response_json, "status", "error");
        cJSON_AddStringToObject(response_json, "error", result ? result : "Tool execution failed");
        if (result) free(result);
    }
    
    char* json_str = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    HttpResponse* response = http_response_json(ret == 0 ? 200 : 500, json_str);
    free(json_str);
    tool_execute_request_free(tool_req);
    
    return response;
}

// GET /sessions
static HttpResponse* handle_sessions(const HttpRequest* request, void* user_data) {
    (void)request;
    (void)user_data;
    
    cJSON* response_json = cJSON_CreateObject();
    cJSON* sessions = cJSON_CreateArray();
    
    // TODO: 列出所有会话
    // 目前只返回默认会话信息
    if (g_agent.session_manager) {
        cJSON* session_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(session_obj, "id", "default");
        cJSON_AddNumberToObject(session_obj, "message_count", 0);
        cJSON_AddItemToArray(sessions, session_obj);
    }
    
    cJSON_AddItemToObject(response_json, "sessions", sessions);
    
    char* json_str = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    HttpResponse* response = http_response_json(200, json_str);
    free(json_str);
    
    return response;
}

// POST /session/clear
static HttpResponse* handle_session_clear(const HttpRequest* request, void* user_data) {
    (void)user_data;
    
    // 解析 session_id
    char* session_id = NULL;
    if (request->body) {
        cJSON* root = cJSON_Parse(request->body);
        if (root) {
            cJSON* sid = cJSON_GetObjectItem(root, "session_id");
            if (sid && cJSON_IsString(sid)) {
                session_id = strdup(sid->valuestring);
            }
            cJSON_Delete(root);
        }
    }
    
    if (!session_id) {
        session_id = strdup("default");
    }
    
    // TODO: 实现清除会话
    
    cJSON* response_json = cJSON_CreateObject();
    cJSON_AddStringToObject(response_json, "status", "success");
    cJSON_AddStringToObject(response_json, "session_id", session_id);
    cJSON_AddStringToObject(response_json, "message", "Session cleared");
    
    char* json_str = cJSON_Print(response_json);
    cJSON_Delete(response_json);
    
    HttpResponse* response = http_response_json(200, json_str);
    free(json_str);
    free(session_id);
    
    return response;
}

// 路由处理
static HttpResponse* route_request(const HttpRequest* request, void* user_data) {
    const char* path = request->path;
    
    log_debug("Routing request: %s %s", request->method, path);
    
    // POST /chat
    if (strcmp(path, "/chat") == 0 && strcmp(request->method, "POST") == 0) {
        return handle_chat(request, user_data);
    }
    
    // GET /health
    if (strcmp(path, "/health") == 0 && strcmp(request->method, "GET") == 0) {
        return handle_health(request, user_data);
    }
    
    // GET /models
    if (strcmp(path, "/models") == 0 && strcmp(request->method, "GET") == 0) {
        return handle_models(request, user_data);
    }
    
    // POST /model/switch
    if (strcmp(path, "/model/switch") == 0 && strcmp(request->method, "POST") == 0) {
        return handle_model_switch(request, user_data);
    }
    
    // GET /tools
    if (strcmp(path, "/tools") == 0 && strcmp(request->method, "GET") == 0) {
        return handle_tools(request, user_data);
    }
    
    // POST /tools/execute
    if (strcmp(path, "/tools/execute") == 0 && strcmp(request->method, "POST") == 0) {
        return handle_tools_execute(request, user_data);
    }
    
    // GET /sessions
    if (strcmp(path, "/sessions") == 0 && strcmp(request->method, "GET") == 0) {
        return handle_sessions(request, user_data);
    }
    
    // POST /session/clear
    if (strcmp(path, "/session/clear") == 0 && strcmp(request->method, "POST") == 0) {
        return handle_session_clear(request, user_data);
    }
    
    // 404
    return http_response_error(404, "Not found");
}

// ==================== 公共 API ====================

HttpServer* http_api_init(int port) {
    HttpServerConfig config = {
        .port = port,
        .max_connections = 100,
        .handler = route_request,
        .stream_handler = handle_chat_stream,
        .user_data = NULL
    };
    
    return http_server_create(&config);
}

bool http_api_start(HttpServer* server) {
    return http_server_start(server);
}

void http_api_stop(HttpServer* server) {
    http_server_stop(server);
}

void http_api_cleanup(HttpServer* server) {
    http_server_destroy(server);
}
