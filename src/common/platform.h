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
// Windows uses WSAEWOULDBLOCK, Unix uses EINPROGRESS
#define WSAEWOULDBLOCK WSAEWOULDBLOCK
#define SOCKET_WOULD_BLOCK(e) ((e) == WSAEWOULDBLOCK)
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
#define WSAEWOULDBLOCK EINPROGRESS
#define SOCKET_WOULD_BLOCK(e) ((e) == EINPROGRESS)
#include <dlfcn.h>

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
 * Get path separator character
 * 
 * @return Path separator character ('\\' on Windows, '/' on Unix)
 */
char platform_path_separator(void);

/**
 * Check if path is absolute
 * 
 * @param path Path to check
 * @return true if absolute, false otherwise
 */
bool platform_is_absolute_path(const char* path);

/**
 * Normalize path separators for current platform
 * Converts '/' to '\\' on Windows, and '\\' to '/' on Unix
 * 
 * @param path Path to normalize (modified in place)
 */
void platform_normalize_path(char* path);

/**
 * Join path components
 * 
 * @param dest Destination buffer
 * @param dest_size Size of destination buffer
 * @param ... Path components (NULL-terminated list)
 */
void platform_path_join(char* dest, size_t dest_size, ...);

/**
 * Initialize platform-specific console
 * Sets UTF-8 mode on Windows, no-op on Unix
 */
void platform_console_init(void);

/**
 * Set socket to non-blocking mode
 * 
 * @param sock Socket handle
 * @return true on success, false on failure
 */
bool platform_socket_set_nonblocking(SOCKET sock);

/**
 * Set socket to blocking mode
 * 
 * @param sock Socket handle
 * @return true on success, false on failure
 */
bool platform_socket_set_blocking(SOCKET sock);

/**
 * Check if last error is "would block"
 * 
 * @return true if error is WSAEWOULDBLOCK/EWOULDBLOCK/EAGAIN
 */
bool platform_socket_would_block(void);

/**
 * Prepare command for execution
 * On Windows, prepends "chcp 65001 >nul && " to set UTF-8 code page
 * 
 * @param cmd Original command
 * @param buf Buffer to store prepared command
 * @param buf_size Size of buffer
 */
void platform_prepare_command(const char* cmd, char* buf, size_t buf_size);

/**
 * Set socket option
 * 
 * @param sock Socket handle
 * @param level Protocol level
 * @param optname Option name
 * @param optval Option value
 * @param optlen Option length
 * @return 0 on success, -1 on failure
 */
int platform_setsockopt(SOCKET sock, int level, int optname, const void* optval, socklen_t optlen);

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
