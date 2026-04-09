#ifndef PLATFORM_H
#define PLATFORM_H

#ifdef _WIN32
// 避免头文件冲突 - 先防止 sys/socket.h 和 sys/select.h 被包含
#define _SYS_SOCKET_H
#define _SYS_SOCKET_H_
#define _SOCKET_H
#define _SELECT_H
#define __SYS_SOCKET_H__
#define __SOCKET_H__
#define __SELECT_H__
// 然后包含 winsock2.h 和 ws2tcpip.h
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#define sleep(sec) Sleep((sec) * 1000)
#define msleep(ms) Sleep(ms)
#define CLOSESOCKET(s) closesocket(s)
#define WSAGetLastError() GetLastError()
#else
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#define sleep(sec) sleep(sec)
#define msleep(ms) do { struct timespec ts = {0, (ms)*1000000L}; nanosleep(&ts, NULL); } while(0)
#define CLOSESOCKET(s) close(s)
#define SOCKET int
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define MAKEWORD(a, b) ((a) | ((b) << 8))
#define WSAGetLastError() errno
#define closesocket close
#define WSADATA int
#define WSAStartup(a, b) (0)
#define WSACleanup() (0)
#endif

#include <stdbool.h>

/**
 * Platform-specific utility functions
 */

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
 * Check if path is a directory
 * 
 * @param path Path to check
 * @return true if directory, false otherwise
 */
bool platform_is_dir(const char* path);

/**
 * Get current working directory
 * 
 * @param buf Buffer to store current working directory
 * @param size Size of buffer
 * @return buf on success, NULL on failure
 */
char* platform_getcwd(char* buf, size_t size);

/**
 * Initialize platform-specific network functionality
 * 
 * @return true on success, false on failure
 */
bool platform_network_init(void);

/**
 * Cleanup platform-specific network functionality
 */
void platform_network_cleanup(void);

/**
 * Get last network error
 * 
 * @return Error code
 */
int platform_get_last_error(void);

/**
 * Load dynamic library
 * 
 * @param path Library path
 * @return Library handle, or NULL on failure
 */
void* platform_load_library(const char* path);

/**
 * Unload dynamic library
 * 
 * @param handle Library handle
 */
void platform_unload_library(void* handle);

/**
 * Get function from dynamic library
 * 
 * @param handle Library handle
 * @param name Function name
 * @return Function pointer, or NULL on failure
 */
void* platform_get_function(void* handle, const char* name);

/**
 * Get dynamic library error message
 * 
 * @return Error message
 */
const char* platform_dlerror(void);

/**
 * Get plugin file extension
 * 
 * @return Plugin file extension (e.g., ".dll" or ".so")
 */
const char* platform_get_plugin_ext(void);

#endif // PLATFORM_H
