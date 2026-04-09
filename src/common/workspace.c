#include "workspace.h"
#include "platform.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static char* g_workspace_path = NULL;

// Create directory if it doesn't exist
static bool create_directory(const char* path) {
    if (platform_exists(path)) {
        // Directory exists
        return true;
    }
    
    // Create directory recursively
    return platform_mkdir_p(path);
}

// Initialize workspace
bool workspace_init(const char* workspace_path) {
    if (!workspace_path) {
        return false;
    }
    
    // Create workspace directory
    if (!create_directory(workspace_path)) {
        return false;
    }
    
    // Create subdirectories
    char sessions_dir[512];
    snprintf(sessions_dir, sizeof(sessions_dir), "%s/sessions", workspace_path);
    if (!create_directory(sessions_dir)) {
        return false;
    }
    
    char memory_dir[512];
    snprintf(memory_dir, sizeof(memory_dir), "%s/memory", workspace_path);
    if (!create_directory(memory_dir)) {
        return false;
    }
    
    char skills_dir[512];
    snprintf(skills_dir, sizeof(skills_dir), "%s/skills", workspace_path);
    if (!create_directory(skills_dir)) {
        return false;
    }
    
    // Create workspace files
    char boot_file[512];
    snprintf(boot_file, sizeof(boot_file), "%s/BOOT.md", workspace_path);
    
    FILE* fp = fopen(boot_file, "w");
    if (fp) {
        fprintf(fp, "# CatClaw Workspace\n\nThis is the workspace directory for CatClaw.\n");
        fclose(fp);
    }
    
    char identity_file[512];
    snprintf(identity_file, sizeof(identity_file), "%s/IDENTITY.md", workspace_path);
    
    fp = fopen(identity_file, "w");
    if (fp) {
        fprintf(fp, "# Agent Identity\n\nI am CatClaw, a C-based AI assistant.\n");
        fclose(fp);
    }
    
    char soul_file[512];
    snprintf(soul_file, sizeof(soul_file), "%s/SOUL.md", workspace_path);
    
    fp = fopen(soul_file, "w");
    if (fp) {
        fprintf(fp, "# Agent Soul\n\nI am designed to be helpful, honest, and harmless.\n");
        fclose(fp);
    }
    
    char tools_file[512];
    snprintf(tools_file, sizeof(tools_file), "%s/TOOLS.md", workspace_path);
    
    fp = fopen(tools_file, "w");
    if (fp) {
        fprintf(fp, "# Available Tools\n\n- calculator: Perform basic arithmetic calculations\n- time: Get current time\n- reverse_string: Reverse a string\n- read_file: Read a file from disk\n- write_file: Write content to a file\n- web_search: Simulate web search\n- memory_save: Save a key-value pair to memory\n- memory_load: Load a value from memory by key\n");
        fclose(fp);
    }
    
    // Store workspace path
    g_workspace_path = strdup(workspace_path);
    if (!g_workspace_path) {
        return false;
    }
    
    return true;
}

// Cleanup workspace
void workspace_cleanup(void) {
    if (g_workspace_path) {
        free(g_workspace_path);
        g_workspace_path = NULL;
    }
}

// Get workspace path
char* workspace_get_path(void) {
    return g_workspace_path;
}