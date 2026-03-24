/**
 * Common utility functions implementation
 */
#include "utils.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <sys/types.h>
#include <pwd.h>
#endif

/**
 * Resolve relative path to absolute path
 * If path starts with '/', return as-is (absolute path)
 * If path starts with '~', expand to home directory
 * Otherwise, resolve relative to current working directory
 * 
 * @param path The path to resolve
 * @return Resolved absolute path (caller must free), or NULL on error
 */
char* resolve_path(const char* path) {
    if (!path) return NULL;

    // Absolute path - return copy
    if (path[0] == '/') {
        return strdup(path);
    }

// Windows absolute path (e.g., C:\...)
#ifdef _WIN32
    if (path[0] != '\0' && path[1] == ':') {
        return strdup(path);
    }
#endif

    // Home directory expansion
    if (path[0] == '~') {
        const char* home = getenv("HOME");
#ifdef _WIN32
        if (!home) {
            home = getenv("USERPROFILE");
        }
#else
        if (!home) {
            struct passwd* pw = getpwuid(getuid());
            if (pw) home = pw->pw_dir;
        }
#endif
        if (home) {
            size_t home_len = strlen(home);
            size_t path_len = strlen(path);
            char* resolved = malloc(home_len + path_len + 1);
            if (!resolved) return NULL;
            strcpy(resolved, home);
            strcat(resolved, path + 1);  // Skip the '~'
            return resolved;
        }
    }

    // Relative path - resolve against current working directory
    char cwd[1024];
    if (!getcwd(cwd, sizeof(cwd))) {
        return strdup(path);  // Fallback to original path
    }

    size_t cwd_len = strlen(cwd);
    size_t path_len = strlen(path);

    char* resolved = malloc(cwd_len + path_len + 2);
    if (!resolved) return NULL;

    strcpy(resolved, cwd);
#ifdef _WIN32
    if (cwd_len > 0 && cwd[cwd_len - 1] != '/' && cwd[cwd_len - 1] != '\\') {
        strcat(resolved, "\\");
    }
#else
    if (cwd_len > 0 && cwd[cwd_len - 1] != '/') {
        strcat(resolved, "/");
    }
#endif
    strcat(resolved, path);

    return resolved;
}

char* http_strcasestr(const char* haystack, const char* needle) {
    if (!haystack || !needle) return NULL;
    if (!*needle) return (char*)haystack;
    
    char* h = (char*)haystack;
    while (*h) {
        if (tolower((unsigned char)*h) == tolower((unsigned char)*needle)) {
            char* h2 = h + 1;
            char* n2 = (char*)needle + 1;
            while (*n2 && tolower((unsigned char)*h2) == tolower((unsigned char)*n2)) {
                h2++;
                n2++;
            }
            if (!*n2) return h;
        }
        h++;
    }
    return NULL;
}
