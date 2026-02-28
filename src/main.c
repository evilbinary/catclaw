#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "config.h"
#include "gateway.h"
#include "channels.h"
#include "agent.h"
#include "plugin.h"
#include "log.h"
#include "thread_pool.h"

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

    // Initialize plugin system
    if (!plugin_system_init()) {
        log_error("Failed to initialize plugin system");
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

        if (strcmp(command, "help") == 0) {
            printf("Available commands:\n");
            printf("  help              - Show this help\n");
            printf("  status            - Show status\n");
            printf("  message <text>    - Send a message to AI\n");
            printf("  gateway start     - Start gateway server\n");
            printf("  gateway stop      - Stop gateway server\n");
            printf("  gateway status    - Show gateway status\n");
            printf("  plugin list       - List loaded plugins\n");
            printf("  plugin load <path> - Load a plugin\n");
            printf("  plugin unload <name> - Unload a plugin\n");
            printf("  config list       - List configuration\n");
            printf("  config set <key> <value> - Set configuration\n");
            printf("  config get <key>  - Get configuration\n");
            printf("  channel enable <type> - Enable a channel\n");
            printf("  channel disable <type> - Disable a channel\n");
            printf("  channel connect <type> - Connect a channel\n");
            printf("  channel disconnect <type> - Disconnect a channel\n");
            printf("  model list        - List available AI models\n");
            printf("  model set <model> - Set AI model\n");
            printf("  system restart    - Restart system\n");
            printf("  system shutdown   - Shutdown system\n");
            printf("  exit              - Exit\n");
        } else if (strcmp(command, "status") == 0) {
            printf("Status:\n");
            gateway_status();
            channels_status();
            agent_status();
        } else if (strcmp(command, "exit") == 0) {
            break;
        } else if (strncmp(command, "message", 7) == 0) {
            char *message = command + 8;
            if (*message) {
                agent_send_message(message);
            } else {
                printf("Usage: message <text>\n");
            }
        } else if (strcmp(command, "gateway start") == 0) {
            start_gateway_server();
        } else if (strcmp(command, "gateway stop") == 0) {
            stop_gateway_server();
        } else if (strcmp(command, "gateway status") == 0) {
            gateway_status();
        } else if (strcmp(command, "plugin list") == 0) {
            plugin_list();
        } else if (strncmp(command, "plugin load", 11) == 0) {
            char *path = command + 12;
            if (*path) {
                plugin_load(path);
            } else {
                printf("Usage: plugin load <path>\n");
            }
        } else if (strncmp(command, "plugin unload", 13) == 0) {
            char *name = command + 14;
            if (*name) {
                plugin_unload(name);
            } else {
                printf("Usage: plugin unload <name>\n");
            }
        } else if (strcmp(command, "config list") == 0) {
            config_print();
        } else if (strncmp(command, "config set", 10) == 0) {
            // TODO: Implement config set command
            printf("Config set command not implemented yet\n");
        } else if (strncmp(command, "config get", 10) == 0) {
            // TODO: Implement config get command
            printf("Config get command not implemented yet\n");
        } else if (strncmp(command, "channel enable", 14) == 0) {
            // TODO: Implement channel enable command
            printf("Channel enable command not implemented yet\n");
        } else if (strncmp(command, "channel disable", 15) == 0) {
            // TODO: Implement channel disable command
            printf("Channel disable command not implemented yet\n");
        } else if (strncmp(command, "channel connect", 15) == 0) {
            // TODO: Implement channel connect command
            printf("Channel connect command not implemented yet\n");
        } else if (strncmp(command, "channel disconnect", 18) == 0) {
            // TODO: Implement channel disconnect command
            printf("Channel disconnect command not implemented yet\n");
        } else if (strcmp(command, "model list") == 0) {
            // TODO: Implement model list command
            printf("Model list command not implemented yet\n");
        } else if (strncmp(command, "model set", 8) == 0) {
            // TODO: Implement model set command
            printf("Model set command not implemented yet\n");
        } else if (strcmp(command, "system restart") == 0) {
            // TODO: Implement system restart command
            printf("System restart command not implemented yet\n");
        } else if (strcmp(command, "system shutdown") == 0) {
            // TODO: Implement system shutdown command
            printf("System shutdown command not implemented yet\n");
        } else if (strlen(command) > 0) {
            printf("Unknown command: %s\n", command);
            printf("Type 'help' for available commands\n");
        }
    }

    // Cleanup
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

    printf("\nCatClaw exited\n");
    return 0;
}