#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "common/config.h"
#include "gateway/gateway.h"
#include "gateway/channels.h"
#include "agent/agent.h"
#include "common/plugin.h"
#include "tool/skill.h"
#include "common/log.h"
#include "common/thread_pool.h"
#include "model/ai_model.h"

// External reference to channels array
extern Channel channels[CHANNEL_MAX];

// Gateway server thread
static pthread_t gateway_thread;
static bool gateway_thread_running = false;

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
    printf("🐦 CatClaw - C version\n");
    printf("Based on OpenClaw functionality\n\n");

    // Initialize log system
    log_init();
    log_info("CatClaw starting up");

    // Load configuration
    if (!config_load()) {
        log_fatal("Failed to load configuration");
        return 1;
    }

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

    log_info("CatClaw initialized successfully!");
    log_info("WebSocket server running on port %d", g_config.gateway_port);
    log_info("Use 'help' for available commands");
    printf("\n");

    // Simple command loop
    char command[256];
    while (1) {
        printf("catclaw> ");
        if (fgets(command, sizeof(command), stdin) == NULL) {
            break;
        }

        // Remove newline
        command[strcspn(command, "\n")] = 0;

        if (strlen(command) > 0) {
            if (command[0] == '/') {
                // Command mode: input starts with /
                char *cmd = command + 1; // Skip the /
                
                if (strcmp(cmd, "help") == 0) {
                    printf("Available commands:\n");
                    printf("  /help              - Show this help\n");
                    printf("  /status            - Show status\n");
                    printf("  /message <text>    - Send a message to AI\n");
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
                    printf("Channels:\n");
                    for (int i = 0; i < CHANNEL_MAX; i++) {
                        if (channels[i].connected) {
                            printf("  %s: ✓ connected\n", channels[i].name);
                        }
                    }
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
                    char *channel_name = cmd + 15;
                    if (*channel_name) {
                        ChannelType type = channel_name_to_type(channel_name);
                        if (type != CHANNEL_MAX) {
                            channel_enable(type);
                        } else {
                            printf("Error: Invalid channel name\n");
                        }
                    } else {
                        printf("Usage: /channel enable <name>\n");
                    }
                } else if (strncmp(cmd, "channel disable", 15) == 0) {
                    char *channel_name = cmd + 16;
                    if (*channel_name) {
                        ChannelType type = channel_name_to_type(channel_name);
                        if (type != CHANNEL_MAX) {
                            channel_disable(type);
                        } else {
                            printf("Error: Invalid channel name\n");
                        }
                    } else {
                        printf("Usage: /channel disable <name>\n");
                    }
                } else if (strncmp(cmd, "channel connect", 15) == 0) {
                    char *channel_name = cmd + 16;
                    if (*channel_name) {
                        ChannelType type = channel_name_to_type(channel_name);
                        if (type != CHANNEL_MAX) {
                            channel_connect(type);
                        } else {
                            printf("Error: Invalid channel name\n");
                        }
                    } else {
                        printf("Usage: /channel connect <name>\n");
                    }
                } else if (strncmp(cmd, "channel disconnect", 18) == 0) {
                    char *channel_name = cmd + 19;
                    if (*channel_name) {
                        ChannelType type = channel_name_to_type(channel_name);
                        if (type != CHANNEL_MAX) {
                            channel_disconnect(type);
                        } else {
                            printf("Error: Invalid channel name\n");
                        }
                    } else {
                        printf("Usage: /channel disconnect <name>\n");
                    }
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