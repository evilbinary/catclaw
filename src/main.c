#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "config.h"
#include "gateway.h"
#include "channels.h"
#include "agent.h"

// Gateway server thread
static pthread_t gateway_thread;
static bool gateway_thread_running = false;

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

    // Load configuration
    if (!config_load()) {
        fprintf(stderr, "Failed to load configuration\n");
        return 1;
    }

    // Initialize gateway
    if (!gateway_init()) {
        fprintf(stderr, "Failed to initialize gateway\n");
        return 1;
    }

    // Initialize channels
    if (!channels_init()) {
        fprintf(stderr, "Failed to initialize channels\n");
        return 1;
    }

    // Initialize agent
    if (!agent_init()) {
        fprintf(stderr, "Failed to initialize agent\n");
        return 1;
    }

    // Start gateway server
    if (!start_gateway_server()) {
        fprintf(stderr, "Failed to start gateway server\n");
        // Continue anyway
    }

    printf("CatClaw initialized successfully!\n");
    printf("WebSocket server running on port %d\n", g_config.gateway_port);
    printf("Use 'help' for available commands\n\n");

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
            printf("  help        - Show this help\n");
            printf("  status      - Show status\n");
            printf("  message     - Send a message to AI\n");
            printf("  gateway     - Manage gateway server\n");
            printf("  exit        - Exit\n");
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
        } else if (strlen(command) > 0) {
            printf("Unknown command: %s\n", command);
            printf("Type 'help' for available commands\n");
        }
    }

    // Cleanup
    stop_gateway_server();
    agent_cleanup();
    channels_cleanup();
    gateway_cleanup();
    config_cleanup();

    printf("\nCatClaw exited\n");
    return 0;
}
