#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdbool.h>

/**
 * Platform-specific utility functions
 */

#ifdef _WIN32
#include <windows.h>
#define sleep(sec) Sleep((sec) * 1000)
#define msleep(ms) Sleep(ms)
#define CLOSESOCKET(s) closesocket(s)
#else
#include <unistd.h>
#include <sys/stat.h>
#define sleep(sec) sleep(sec)
#define msleep(ms) do { struct timespec ts = {0, (ms)*1000000L}; nanosleep(&ts, NULL); } while(0)
#define CLOSESOCKET(s) close(s)
#endif

/**
 * Get user profile directory
 * 
 * @return User profile directory path (caller should not free), or NULL on error
 */
const char* platform_get_user_profile(void);

/**
 * Create directory recursively
 * 
 * @param path Directory path to create
 * @return true on success, false on failure
 */
bool platform_mkdir_p(const char* path);

/**
 * Check if file or directory exists
 * 
 * @param path Path to check
 * @return true if exists, false otherwise
 */
bool platform_exists(const char* path);

/**
 * Get current working directory
 * 
 * @param buf Buffer to store current working directory
 * @param size Size of buffer
 * @return buf on success, NULL on failure
 */
char* platform_getcwd(char* buf, size_t size);

#endif // PLATFORM_H
