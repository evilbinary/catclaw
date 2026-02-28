#include "agent.h"
#include "config.h"
#include "channels.h"
#include "ai_model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global agent instance
Agent g_agent = {
    .model = NULL,
    .running = false
};

bool agent_init(void) {
    g_agent.model = strdup(g_config.model);
    if (!g_agent.model) {
        perror("strdup");
        return false;
    }

    // Initialize AI model
    AIModelConfig model_config;
    if (strstr(g_config.model, "anthropic") != NULL) {
        model_config.type = AI_MODEL_ANTHROPIC;
        model_config.model_name = strstr(g_config.model, "/") ? strstr(g_config.model, "/") + 1 : g_config.model;
    } else if (strstr(g_config.model, "openai") != NULL) {
        model_config.type = AI_MODEL_OPENAI;
        model_config.model_name = strstr(g_config.model, "/") ? strstr(g_config.model, "/") + 1 : g_config.model;
    } else {
        model_config.type = AI_MODEL_ANTHROPIC;
        model_config.model_name = "claude-3-opus-20240229";
    }
    model_config.api_key = getenv("ANTHROPIC_API_KEY") ? getenv("ANTHROPIC_API_KEY") : getenv("OPENAI_API_KEY");
    model_config.base_url = NULL;

    if (!ai_model_init(&model_config)) {
        fprintf(stderr, "Failed to initialize AI model\n");
        free(g_agent.model);
        g_agent.model = NULL;
        return false;
    }

    g_agent.running = true;
    printf("Agent initialized with model: %s\n", g_agent.model);
    return true;
}

void agent_cleanup(void) {
    if (g_agent.model) {
        free(g_agent.model);
        g_agent.model = NULL;
    }

    // Cleanup AI model
    ai_model_cleanup();

    g_agent.running = false;
    printf("Agent cleaned up\n");
}

void agent_status(void) {
    printf("  Agent: %s\n", g_agent.running ? "running" : "stopped");
    printf("  Model: %s\n", g_agent.model);
}

bool agent_send_message(const char *message) {
    if (!g_agent.running) {
        fprintf(stderr, "Agent is not running\n");
        return false;
    }

    printf("Agent received message: %s\n", message);

    // Send message to AI model
    AIModelResponse *response = ai_model_send_message(message);
    if (response) {
        if (response->success) {
            printf("Agent response: %s\n", response->content);
            // Send response to WebChat channel
            channel_send_message(CHANNEL_WEBCHAT, response->content);
        } else {
            fprintf(stderr, "AI model error: %s\n", response->error);
            // Send error message to WebChat channel
            channel_send_message(CHANNEL_WEBCHAT, "Error: Failed to get response from AI model");
        }
        ai_model_free_response(response);
    } else {
        fprintf(stderr, "Failed to get response from AI model\n");
        // Send error message to WebChat channel
        channel_send_message(CHANNEL_WEBCHAT, "Error: Failed to get response from AI model");
    }

    return true;
}
