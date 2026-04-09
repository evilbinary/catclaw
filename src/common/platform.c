#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <sys/stat.h>
#include <userenv.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#endif

// Global variable to store user profile directory
static const char* g_user_profile = NULL;

/**
 * Get user profile directory
 * 
 * @return User profile directory path (caller should not free), or NULL on error
 */
const char* platform_get_user_profile(void) {
    if (g_user_profile) {
        return g_user_profile;
    }
    
    // First try environment variables
    const char* home = getenv("USERPROFILE");
    if (!home) {
        home = getenv("HOME");
    }
    if (!home) {
        home = getenv("APPDATA");
    }
    
    // Check if the path exists
    if (home) {
        struct stat st;
        if (stat(home, &st) == 0 && S_ISDIR(st.st_mode)) {
            g_user_profile = home;
            return g_user_profile;
        }
    }
    
    // Try Windows API to get user profile path
    #ifdef _WIN32
    char temp_path[MAX_PATH];
    if (GetUserProfileDirectoryA(NULL, temp_path, &(DWORD){MAX_PATH})) {
        g_user_profile = strdup(temp_path);
        return g_user_profile;
    }
    #else
    // Try getpwuid on non-Windows
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        if (pw) {
            g_user_profile = pw->pw_dir;
            return g_user_profile;
        }
    }
    #endif
    
    g_user_profile = home;
    return g_user_profile;
}

/**
 * Create directory recursively
 * 
 * @param path Directory path to create
 * @return true on success, false on failure
 */
bool platform_mkdir_p(const char* path) {
    if (!path) return false;
    
    char* path_copy = strdup(path);
    if (!path_copy) return false;
    
    char* p = path_copy;
    while (*p) {
        if (*p == '/' || *p == '\\') {
            *p = '\0';
            #ifdef _WIN32
            CreateDirectoryA(path_copy, NULL);
            #else
            mkdir(path_copy, 0755);
            #endif
            *p = (*p == '/') ? '/' : '\\';
        }
        p++;
    }
    
    // Create the final directory
    #ifdef _WIN32
    bool result = CreateDirectoryA(path_copy, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
    #else
    bool result = mkdir(path_copy, 0755) == 0 || errno == EEXIST;
    #endif
    
    free(path_copy);
    return result;
}

/**
 * Check if file or directory exists
 * 
 * @param path Path to check
 * @return true if exists, false otherwise
 */
bool platform_exists(const char* path) {
    if (!path) return false;
    
    struct stat st;
    return stat(path, &st) == 0;
}

/**
 * Get current working directory
 * 
 * @param buf Buffer to store current working directory
 * @param size Size of buffer
 * @return buf on success, NULL on failure
 */
char* platform_getcwd(char* buf, size_t size) {
    #ifdef _WIN32
    if (GetCurrentDirectoryA(size, buf) == 0) {
        return NULL;
    }
    return buf;
    #else
    return getcwd(buf, size);
    #endif
}
