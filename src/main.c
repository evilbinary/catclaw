#include "common/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "common/config.h"
#include "gateway/gateway.h"
#include "gateway/channels.h"
#include "agent/agent.h"
#include "agent/command.h"
#include "common/plugin.h"
#include "tool/skill.h"
#include "common/log.h"
#include "common/thread_pool.h"
#include "model/ai_model.h"

// Thread pool
static ThreadPool *g_thread_pool = NULL;

int main(int argc, char *argv[]) {
    // Initialize platform-specific console
    platform_console_init();
    
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

    // Start gateway (WebSocket server, HTTP API server, Feishu WS clients)
    if (!gateway_start()) {
        log_error("Failed to start gateway");
        // Continue anyway
    }

    log_info("CatClaw initialized successfully!");
    if (g_config.gateway.websocket_enabled) {
        log_info("WebSocket server running on port %d", g_config.gateway_port);
    }
    if (g_config.gateway.http_server_enabled) {
        log_info("HTTP API server running on port %d", g_gateway.http_port);
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
                // 使用统一的命令处理器
                CommandResult* result = command_process(command);
                if (result) {
                    if (result->is_command) {
                        printf("%s\n", result->response);
                        
                        // 处理特殊动作
                        if (result->action == COMMAND_ACTION_EXIT) {
                            command_result_free(result);
                            break;
                        } else if (result->action == COMMAND_ACTION_SEND_MESSAGE) {
                            // 发送消息给 AI
                            agent_send_message(result->response);
                        } else if (result->action == COMMAND_ACTION_SHUTDOWN) {
                            command_result_free(result);
                            // 清理并退出
                            gateway_stop();
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
                            printf("CatClaw shutdown\n");
                            exit(0);
                        } else if (result->action == COMMAND_ACTION_RESTART) {
                            printf("Restart functionality not yet implemented\n");
                        }
                    }
                    command_result_free(result);
                }
            } else {
                // Conversation mode: input does not start with /
                // Send message to agent for processing
                agent_send_message(command);
            }
        }
    }

    // Cleanup
    gateway_stop();
    agent_stop_worker_thread();
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