#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "common/config.h"
#include "gateway/gateway.h"
#include "gateway/channels.h"
#include "gateway/feishu_ws.h"
#include "agent/agent.h"
#include "agent/command.h"
#include "common/plugin.h"
#include "tool/skill.h"
#include "common/log.h"
#include "common/thread_pool.h"
#include "gateway/http_api.h"
#include "model/ai_model.h"

// Gateway server thread
static pthread_t gateway_thread;
static bool gateway_thread_running = false;

// HTTP API server
static HttpServer* g_http_api_server = NULL;

// Feishu WebSocket client
static FeishuWsClient* g_feishu_ws_client = NULL;

// Thread pool
static ThreadPool *g_thread_pool = NULL;

// Thread function for gateway server
static void *gateway_server_thread(void *arg) {
    gateway_start();
    gateway_thread_running = false;
    return NULL;
}

// Start gateway server in a separate thread
static bool start_gateway_server(void) {
    if (gateway_thread_running) {
        printf("Gateway server is already running\n");
        return true;
    }

    gateway_thread_running = true;
    int ret = pthread_create(&gateway_thread, NULL, gateway_server_thread, NULL);
    if (ret != 0) {
        fprintf(stderr, "Failed to create gateway thread: %d\n", ret);
        gateway_thread_running = false;
        return false;
    }

    printf("Gateway server started in background\n");
    return true;
}

// Stop gateway server
static void stop_gateway_server(void) {
    if (!gateway_thread_running) {
        printf("Gateway server is not running\n");
        return;
    }

    gateway_stop();
    pthread_join(gateway_thread, NULL);
    gateway_thread_running = false;
    printf("Gateway server stopped\n");
}

int main(int argc, char *argv[]) {
#ifdef _WIN32
    // Set console to UTF-8 mode for proper Chinese character display
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    printf("🐦 CatClaw - C version\n");
    printf("Based on OpenClaw functionality\n\n");

    // Parse command-line arguments for log level
    const char *cli_log_level = NULL;
    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "--log-level") == 0 || strcmp(argv[i], "-l") == 0) && i + 1 < argc) {
            cli_log_level = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: catclaw [options]\n\n");
            printf("Options:\n");
            printf("  -l, --log-level <level>  Set log level: debug, info, warn, error, fatal\n");
            printf("  -h, --help               Show this help\n");
            return 0;
        }
    }

    // Load configuration
    if (!config_load()) {
        fprintf(stderr, "Failed to load configuration\n");
        return 1;
    }

    // Initialize log system
    log_init();
    // Set log level based on CLI argument, then config, then debug flag
    LogLevel log_level = LOG_LEVEL_INFO;

    if (cli_log_level && cli_log_level[0] != '\0') {
        if (strcmp(cli_log_level, "debug") == 0) {
            log_level = LOG_LEVEL_DEBUG;
        } else if (strcmp(cli_log_level, "info") == 0) {
            log_level = LOG_LEVEL_INFO;
        } else if (strcmp(cli_log_level, "warn") == 0) {
            log_level = LOG_LEVEL_WARN;
        } else if (strcmp(cli_log_level, "error") == 0) {
            log_level = LOG_LEVEL_ERROR;
        } else if (strcmp(cli_log_level, "fatal") == 0) {
            log_level = LOG_LEVEL_FATAL;
        } else {
            fprintf(stderr, "Unknown log level: %s (use: debug, info, warn, error, fatal)\n", cli_log_level);
        }
    } else {
        const char *loglevel_str = g_config.logging.level;
        if (loglevel_str && loglevel_str[0] != '\0') {
            printf("[DEBUG] Setting log level from config: %s\n", loglevel_str);
            if (strcmp(loglevel_str, "debug") == 0) {
                log_level = LOG_LEVEL_DEBUG;
            } else if (strcmp(loglevel_str, "info") == 0) {
                log_level = LOG_LEVEL_INFO;
            } else if (strcmp(loglevel_str, "warn") == 0) {
                log_level = LOG_LEVEL_WARN;
            } else if (strcmp(loglevel_str, "error") == 0) {
                log_level = LOG_LEVEL_ERROR;
            } else if (strcmp(loglevel_str, "fatal") == 0) {
                log_level = LOG_LEVEL_FATAL;
            }
        } else if (g_config.debug) {
            // Fallback to debug flag
            printf("[DEBUG] Setting log level from debug flag\n");
            log_level = LOG_LEVEL_DEBUG;
        }
    }
    
    log_set_level(log_level);
    log_info("CatClaw starting up");

    // Initialize gateway
    if (!gateway_init()) {
        log_fatal("Failed to initialize gateway");
        return 1;
    }

    // Initialize channels (channels_init 内部已加载配置)
    if (!channels_init()) {
        log_fatal("Failed to initialize channels");
        return 1;
    }

    // Initialize agent
    if (!agent_init()) {
        log_fatal("Failed to initialize agent");
        return 1;
    }

    // Start worker thread
    if (!agent_start_worker_thread()) {
        log_error("Failed to start worker thread");
        // Continue anyway
    }

    // Initialize plugin system
    if (!plugin_system_init()) {
        log_error("Failed to initialize plugin system");
        // Continue anyway
    }

    // Initialize skill system
    if (!skill_system_init()) {
        log_error("Failed to initialize skill system");
        // Continue anyway
    }

    // Initialize thread pool
    g_thread_pool = thread_pool_create(4, 100);
    if (!g_thread_pool) {
        log_error("Failed to initialize thread pool");
        // Continue anyway
    }

    // Start gateway server (only if websocket is enabled)
    if (g_config.gateway.websocket_enabled) {
        if (!start_gateway_server()) {
            log_error("Failed to start gateway server");
            // Continue anyway
        }
    } else {
        log_info("WebSocket server is disabled");
    }

    // Start HTTP API server (only if http_server is enabled)
    if (g_config.gateway.http_server_enabled) {
        int http_port = g_config.http_port > 0 ? g_config.http_port : 8080;
        g_http_api_server = http_api_init(http_port);
        if (g_http_api_server) {
            if (http_api_start(g_http_api_server)) {
                log_info("HTTP API server started on port %d", http_port);
            } else {
                log_error("Failed to start HTTP API server");
            }
        } else {
            log_error("Failed to initialize HTTP API server");
        }
    } else {
        log_info("HTTP API server is disabled");
    }
    
    // Start Feishu WebSocket client (for channels with ws_enabled=true)
    for (int i = 0; i < g_config.channels.count; i++) {
        ChannelConfigEntry *ch = &g_config.channels.channels[i];
        if (ch->type && strcmp(ch->type, "feishu") == 0 && 
            ch->ws_enabled && ch->app_id && ch->app_secret) {
            g_feishu_ws_client = feishu_ws_create(ch->app_id, ch->app_secret);
            if (g_feishu_ws_client) {
                if (ch->ws_domain) {
                    feishu_ws_set_domain(g_feishu_ws_client, ch->ws_domain);
                }
                if (ch->ws_ping_interval > 0) {
                    feishu_ws_set_ping_interval(g_feishu_ws_client, ch->ws_ping_interval);
                }
                if (ch->ws_reconnect_interval > 0 || ch->ws_max_reconnect != 0) {
                    feishu_ws_set_reconnect(g_feishu_ws_client, 
                                           ch->ws_reconnect_interval > 0 ? ch->ws_reconnect_interval : 120,
                                           ch->ws_max_reconnect);
                }
                
                if (feishu_ws_start(g_feishu_ws_client)) {
                    log_info("Feishu WebSocket client started for channel: %s", ch->id);
                } else {
                    log_error("Failed to start Feishu WebSocket client for channel: %s", ch->id);
                    feishu_ws_destroy(g_feishu_ws_client);
                    g_feishu_ws_client = NULL;
                }
            }
        }
    }

    log_info("CatClaw initialized successfully!");
    if (g_config.gateway.websocket_enabled) {
        log_info("WebSocket server running on port %d", g_config.gateway_port);
    }
    if (g_config.gateway.http_server_enabled && g_http_api_server) {
        int http_port = g_config.http_port > 0 ? g_config.http_port : 8080;
        log_info("HTTP API server running on port %d", http_port);
    }
    log_info("Use 'help' for available commands");
    printf("\n");

    // Simple command loop
    char command[1024];  // Increase buffer size for UTF-8 characters
    while (1) {
        printf("catclaw> ");
        fflush(stdout);
        
        // Use standard fgets for all platforms
        if (fgets(command, sizeof(command), stdin) == NULL) {
            break;
        }
        command[strcspn(command, "\n")] = 0;
        
        // Debug: print raw input
        // log_debug("Raw input length: %zu, content: ", strlen(command));
        // for (size_t i = 0; i < strlen(command); i++) {
        //     log_debug("%02x ", (unsigned char)command[i]);
        // }
        // log_debug("\n");

        if (strlen(command) > 0) {
            if (command[0] == '/') {
                // Command mode: input starts with /
                
                // CLI 特有命令处理
                char *cmd = command + 1; // Skip the /
                
                // /message 命令 - 直接发送消息
                if (strncmp(cmd, "message ", 8) == 0) {
                    char *message = cmd + 8;
                    if (*message) {
                        agent_send_message(message);
                    } else {
                        printf("Usage: /message <text>\n");
                    }
                }
                // /skill 命令 - CLI 特有
                else if (strncmp(cmd, "skill load ", 11) == 0) {
                    char *path = cmd + 11;
                    if (*path) {
                        agent_load_skill(path);
                    } else {
                        printf("Usage: /skill load <path>\n");
                    }
                } else if (strncmp(cmd, "skill unload ", 13) == 0) {
                    char *name = cmd + 13;
                    if (*name) {
                        agent_unload_skill(name);
                    } else {
                        printf("Usage: /skill unload <name>\n");
                    }
                } else if (strncmp(cmd, "skill execute ", 14) == 0) {
                    char *params = cmd + 14;
                    char *name = strtok(params, " ");
                    char *skill_params = strtok(NULL, "");
                    if (name) {
                        char *result = agent_execute_skill(name, skill_params);
                        printf("%s\n", result);
                        free(result);
                    } else {
                        printf("Usage: /skill execute <name> [params]\n");
                    }
                } else if (strcmp(cmd, "skills list") == 0) {
                    agent_list_skills();
                } else if (strncmp(cmd, "skill enable ", 12) == 0) {
                    char *name = cmd + 12;
                    if (*name) {
                        agent_enable_skill(name);
                    } else {
                        printf("Usage: /skill enable <name>\n");
                    }
                } else if (strncmp(cmd, "skill disable ", 13) == 0) {
                    char *name = cmd + 13;
                    if (*name) {
                        agent_disable_skill(name);
                    } else {
                        printf("Usage: /skill disable <name>\n");
                    }
                }
                // /channel connect/disconnect - CLI 特有
                else if (strncmp(cmd, "channel connect ", 16) == 0) {
                    char *channel_id = cmd + 16;
                    if (*channel_id) {
                        ChannelInstance *ch = channel_find(channel_id);
                        if (ch) {
                            channel_connect(ch);
                        } else {
                            printf("Error: Channel not found: %s\n", channel_id);
                        }
                    } else {
                        printf("Usage: /channel connect <id>\n");
                    }
                } else if (strncmp(cmd, "channel disconnect ", 18) == 0) {
                    char *channel_id = cmd + 18;
                    if (*channel_id) {
                        ChannelInstance *ch = channel_find(channel_id);
                        if (ch) {
                            channel_disconnect(ch);
                        } else {
                            printf("Error: Channel not found: %s\n", channel_id);
                        }
                    } else {
                        printf("Usage: /channel disconnect <id>\n");
                    }
                }
                // /system shutdown - CLI 特有
                else if (strcmp(cmd, "system shutdown") == 0) {
                    printf("Shutting down CatClaw...\n");
                    // Cleanup and exit
                    stop_gateway_server();
                    agent_cleanup();
                    channels_cleanup();
                    plugin_system_cleanup();
                    if (g_thread_pool) {
                        thread_pool_destroy(g_thread_pool);
                    }
                    gateway_cleanup();
                    config_cleanup();
                    log_info("CatClaw exiting");
                    log_cleanup();
                    printf("CatClaw shutdown\n");
                    exit(0);
                } else if (strcmp(cmd, "system restart") == 0) {
                    printf("Restarting CatClaw...\n");
                    printf("Restart functionality not yet implemented\n");
                }
                // 使用统一的命令处理器
                else {
                    CommandResult* result = command_process(command);
                    if (result) {
                        if (result->is_command) {
                            printf("%s\n", result->response);
                            
                            // 处理特殊动作
                            if (result->action == COMMAND_ACTION_EXIT) {
                                command_result_free(result);
                                break;
                            }
                        }
                        command_result_free(result);
                    }
                }
            } else {
                // Conversation mode: input does not start with /
                // Send message to agent for processing
                agent_send_message(command);
            }
        }
    }

    // Cleanup
    stop_gateway_server();
    agent_stop_worker_thread();
    
    // Stop Feishu WebSocket client
    if (g_feishu_ws_client) {
        feishu_ws_stop(g_feishu_ws_client);
        feishu_ws_destroy(g_feishu_ws_client);
        g_feishu_ws_client = NULL;
    }
    
    // Stop HTTP API server
    if (g_http_api_server) {
        http_api_stop(g_http_api_server);
        http_api_cleanup(g_http_api_server);
        g_http_api_server = NULL;
    }
    
    agent_cleanup();
    channels_cleanup();
    plugin_system_cleanup();
    skill_system_cleanup();
    if (g_thread_pool) {
        thread_pool_destroy(g_thread_pool);
    }
    gateway_cleanup();
    config_cleanup();
    log_info("CatClaw exiting");
    log_cleanup();

    printf("\nCatClaw exited\n");
    return 0;
}