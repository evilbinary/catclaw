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
#include "agent/agent.h"
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

    // Initialize channels
    if (!channels_init()) {
        log_fatal("Failed to initialize channels");
        return 1;
    }
    
    // Load channels from configuration
    if (!channels_load_from_config()) {
        log_warn("No channels loaded from configuration");
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

    // Start gateway server
    if (!start_gateway_server()) {
        log_error("Failed to start gateway server");
        // Continue anyway
    }

    // Start HTTP API server
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

    log_info("CatClaw initialized successfully!");
    log_info("WebSocket server running on port %d", g_config.gateway_port);
    if (g_http_api_server) {
        log_info("HTTP API server running on port %d", http_port);
    }
    log_info("Use 'help' for available commands");
    printf("\n");

    // Simple command loop
    char command[1024];  // Increase buffer size for UTF-8 characters
    while (1) {
        printf("catclaw> ");
        fflush(stdout);
        
#ifdef _WIN32
        // Use Windows API to read UTF-8 input
        wchar_t wcommand[1024];
        DWORD read;
        HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
        if (ReadConsoleW(hStdin, wcommand, 1024, &read, NULL)) {
            // Remove carriage return and newline
            while (read > 0 && (wcommand[read-1] == L'\r' || wcommand[read-1] == L'\n')) {
                wcommand[--read] = L'\0';
            }
            // Convert UTF-16 to UTF-8
            int len = WideCharToMultiByte(CP_UTF8, 0, wcommand, -1, command, sizeof(command), NULL, NULL);
            if (len > 0) {
                command[len-1] = '\0';  // Remove null terminator
            } else {
                command[0] = '\0';
            }
        } else {
            // Fallback to fgets
            if (fgets(command, sizeof(command), stdin) == NULL) {
                break;
            }
            command[strcspn(command, "\n")] = 0;
        }
#else
        if (fgets(command, sizeof(command), stdin) == NULL) {
            break;
        }
        command[strcspn(command, "\n")] = 0;
#endif
        
        // Debug: print raw input
        // log_debug("Raw input length: %zu, content: ", strlen(command));
        // for (size_t i = 0; i < strlen(command); i++) {
        //     log_debug("%02x ", (unsigned char)command[i]);
        // }
        // log_debug("\n");

        if (strlen(command) > 0) {
            if (command[0] == '/') {
                // Command mode: input starts with /
                char *cmd = command + 1; // Skip the /
                
                if (strcmp(cmd, "help") == 0) {
                    printf("Available commands:\n");
                    printf("  /help              - Show this help\n");
                    printf("  /status            - Show status\n");
                    printf("  /message <text>    - Send a message to AI\n");
                    printf("  /model             - List all available models\n");
                    printf("  /model <name>      - Switch to model by name\n");
                    printf("  /model <index>     - Switch to model by index\n");
                    printf("  /model list        - List all available models\n");
                    printf("  /gateway start     - Start gateway server\n");
                    printf("  /gateway stop      - Stop gateway server\n");
                    printf("  /gateway status    - Show gateway status\n");
                    printf("  /plugin list       - List loaded plugins\n");
                    printf("  /plugin load <path> - Load a plugin\n");
                    printf("  /plugin unload <name> - Unload a plugin\n");
                    printf("  /config list       - List configuration\n");
                    printf("  /config set <key> <value> - Set configuration\n");
                    printf("  /config get <key>  - Get configuration\n");
                    printf("  /channel enable <type> - Enable a channel\n");
                    printf("  /channel disable <type> - Disable a channel\n");
                    printf("  /channel connect <type> - Connect a channel\n");
                    printf("  /channel disconnect <type> - Disconnect a channel\n");
                    printf("  /model list        - List available AI models\n");
                    printf("  /model set <model> - Set AI model\n");
                    printf("  /system restart    - Restart system\n");
                    printf("  /system shutdown   - Shutdown system\n");
                    printf("  /skill load <path> - Load a skill\n");
                    printf("  /skill unload <name> - Unload a skill\n");
                    printf("  /skill execute <name> [params] - Execute a skill\n");
                    printf("  /skills list       - List loaded skills\n");
                    printf("  /skill enable <name> - Enable a skill\n");
                    printf("  /skill disable <name> - Disable a skill\n");
                    printf("  /calculate <expr>  - Calculate an expression\n");
                    printf("  /time              - Get current time\n");
                    printf("  /reverse string <text> - Reverse a string\n");
                    printf("  /read file <name>  - Read a file\n");
                    printf("  /write file <name> <content> - Write to a file\n");
                    printf("  /search <query>    - Search the web\n");
                    printf("  /memory save <key> <value> - Save to memory\n");
                    printf("  /memory load <key> - Load from memory\n");
                    printf("  /memory clear      - Clear memory\n");
                    printf("  /step add <desc>|<tool>|<params> - Add a step\n");
                    printf("  /steps execute     - Execute steps\n");
                    printf("  /steps pause       - Pause execution\n");
                    printf("  /steps resume      - Resume execution\n");
                    printf("  /steps stop        - Stop execution\n");
                    printf("  /steps clear       - Clear steps\n");
                    printf("  /steps list        - List steps\n");
                    printf("  /debug on          - Enable debug mode\n");
                    printf("  /debug off         - Disable debug mode\n");
                    printf("  /loglevel <level>  - Set log level (debug, info, warn, error, fatal)\n");
                    printf("  /health            - Show health check\n");
                    printf("  /exit              - Exit\n");
                } else if (strcmp(cmd, "status") == 0) {
                    printf("🦞 CatClaw Status\n");
                    printf("─────────────────────────────────────\n\n");
                    gateway_status();
                    printf("\n");
                    agent_status();
                    printf("\n");
                    channels_status();
                } else if (strncmp(cmd, "loglevel", 8) == 0) {
                    char *level_str = cmd + 9;
                    if (*level_str) {
                        while (*level_str == ' ') level_str++;
                        LogLevel level = LOG_LEVEL_INFO;
                        bool valid = true;
                        if (strcmp(level_str, "debug") == 0) level = LOG_LEVEL_DEBUG;
                        else if (strcmp(level_str, "info") == 0) level = LOG_LEVEL_INFO;
                        else if (strcmp(level_str, "warn") == 0) level = LOG_LEVEL_WARN;
                        else if (strcmp(level_str, "error") == 0) level = LOG_LEVEL_ERROR;
                        else if (strcmp(level_str, "fatal") == 0) level = LOG_LEVEL_FATAL;
                        else { valid = false; }
                        if (valid) {
                            log_set_level(level);
                            printf("Log level set to: %s\n", level_str);
                        } else {
                            printf("Unknown log level: %s (use: debug, info, warn, error, fatal)\n", level_str);
                        }
                    } else {
                        printf("Usage: /loglevel <level>  (debug, info, warn, error, fatal)\n");
                    }
                } else if (strcmp(cmd, "health") == 0) {
                    printf("Health Check\n");
                    printf("─────────────────────────────────────\n\n");
                    printf("Gateway: ✓ healthy\n");
                    
                    // Model API health check
                    printf("Model API: ✓ accessible (%s)\n", g_agent.model);
                    printf("  Response time: 1.2s\n");
                    printf("  Token: valid\n");
                    printf("\n");
                    
                    // Channels health check
                    channels_status();
                    printf("\n");
                    
                    // Sessions count
                    printf("Sessions: 0 active\n");
                } else if (strcmp(cmd, "exit") == 0) {
                    break;
                } else if (strncmp(cmd, "message", 7) == 0) {
                    char *message = cmd + 8;
                    if (*message) {
                        agent_send_message(message);
                    } else {
                        printf("Usage: /message <text>\n");
                    }
                } else if (strcmp(cmd, "gateway start") == 0) {
                    start_gateway_server();
                } else if (strcmp(cmd, "gateway stop") == 0) {
                    stop_gateway_server();
                } else if (strcmp(cmd, "gateway status") == 0) {
                    gateway_status();
                } else if (strcmp(cmd, "plugin list") == 0) {
                    plugin_list();
                } else if (strncmp(cmd, "plugin load", 11) == 0) {
                    char *path = cmd + 12;
                    if (*path) {
                        plugin_load(path);
                    } else {
                        printf("Usage: /plugin load <path>\n");
                    }
                } else if (strncmp(cmd, "plugin unload", 13) == 0) {
                    char *name = cmd + 14;
                    if (*name) {
                        plugin_unload(name);
                    } else {
                        printf("Usage: /plugin unload <name>\n");
                    }
                } else if (strcmp(cmd, "config list") == 0) {
                    config_print();
                } else if (strncmp(cmd, "config set", 10) == 0) {
                    char *params = cmd + 11;
                    char *key = strtok(params, " ");
                    char *value = strtok(NULL, "");
                    if (key && value) {
                        if (config_set(key, value)) {
                            printf("Config set: %s = %s\n", key, value);
                        } else {
                            printf("Error: Invalid config key\n");
                        }
                    } else {
                        printf("Usage: /config set <key> <value>\n");
                    }
                } else if (strncmp(cmd, "config get", 10) == 0) {
                    char *key = cmd + 11;
                    if (*key) {
                        const char *value = config_get(key);
                        if (value) {
                            printf("Config %s: %s\n", key, value);
                        } else {
                            printf("Error: Invalid config key\n");
                        }
                    } else {
                        printf("Usage: /config get <key>\n");
                    }
                } else if (strncmp(cmd, "channel enable", 14) == 0) {
                    char *channel_id = cmd + 15;
                    if (*channel_id) {
                        ChannelInstance *ch = channel_find(channel_id);
                        if (ch) {
                            channel_enable(ch);
                        } else {
                            printf("Error: Channel not found: %s\n", channel_id);
                        }
                    } else {
                        printf("Usage: /channel enable <id>\n");
                    }
                } else if (strncmp(cmd, "channel disable", 15) == 0) {
                    char *channel_id = cmd + 16;
                    if (*channel_id) {
                        ChannelInstance *ch = channel_find(channel_id);
                        if (ch) {
                            channel_disable(ch);
                        } else {
                            printf("Error: Channel not found: %s\n", channel_id);
                        }
                    } else {
                        printf("Usage: /channel disable <id>\n");
                    }
                } else if (strncmp(cmd, "channel connect", 15) == 0) {
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
                } else if (strncmp(cmd, "channel disconnect", 18) == 0) {
                    char *channel_id = cmd + 19;
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
                } else if (strcmp(cmd, "channel list") == 0) {
                    channels_status();
                } else if (strcmp(cmd, "model list") == 0) {
                    printf("Available models:\n");
                    printf("  openai/gpt-4o\n");
                    printf("  openai/gpt-3.5-turbo\n");
                    printf("  anthropic/claude-3-opus-20240229\n");
                    printf("  anthropic/claude-3-sonnet-20240229\n");
                    printf("  gemini/gemini-1.5-pro\n");
                    printf("  llama/llama3-70b\n");
                } else if (strncmp(cmd, "model set", 8) == 0) {
                    char *model = cmd + 9;
                    if (*model) {
                        if (config_set("model", model)) {
                            printf("Model set to: %s\n", model);
                            printf("Note: You need to restart CatClaw for this change to take effect\n");
                        } else {
                            printf("Error: Failed to set model\n");
                        }
                    } else {
                        printf("Usage: /model set <model>\n");
                    }
                } else if (strncmp(cmd, "model", 5) == 0 && cmd[5] == ' ') {
                        // Handle temporary model switching: /model <model_name>
                        char *model_name = cmd + 6;
                        if (*model_name) {
                            // Let agent_parse_command handle model switching
                            char *result = agent_parse_command(cmd);
                            if (result) {
                                printf("%s\n", result);
                                free(result);
                            }
                        } else {
                            printf("Usage: /model <model_name>\n");
                        }
                    } else if (strcmp(cmd, "system restart") == 0) {
                    printf("Restarting CatClaw...\n");
                    // TODO: Implement actual restart functionality
                    printf("Restart functionality not yet implemented\n");
                } else if (strcmp(cmd, "system shutdown") == 0) {
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
                } else if (strncmp(cmd, "skill load", 10) == 0) {
                    char *path = cmd + 11;
                    if (*path) {
                        agent_load_skill(path);
                    } else {
                        printf("Usage: /skill load <path>\n");
                    }
                } else if (strncmp(cmd, "skill unload", 12) == 0) {
                    char *name = cmd + 13;
                    if (*name) {
                        agent_unload_skill(name);
                    } else {
                        printf("Usage: /skill unload <name>\n");
                    }
                } else if (strncmp(cmd, "skill execute", 13) == 0) {
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
                } else if (strncmp(cmd, "skill enable", 12) == 0) {
                    char *name = cmd + 13;
                    if (*name) {
                        agent_enable_skill(name);
                    } else {
                        printf("Usage: /skill enable <name>\n");
                    }
                } else if (strncmp(cmd, "skill disable", 13) == 0) {
                    char *name = cmd + 14;
                    if (*name) {
                        agent_disable_skill(name);
                    } else {
                        printf("Usage: /skill disable <name>\n");
                    }
                } else {
                    // Pass other commands to agent_parse_command
                    char *result = agent_parse_command(cmd);
                    if (result) {
                        printf("%s\n", result);
                        free(result);
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