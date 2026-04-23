/**
 * Common utility functions
 */
#ifndef COMMON_UTILS_H
#define COMMON_UTILS_H

#include <stddef.h>
#include <stdbool.h>

/**
 * Case-insensitive string search
 * Similar to strstr() but ignores case
 *
 * @param haystack The string to search in
 * @param needle The substring to search for
 * @return Pointer to the first occurrence, or NULL if not found
 */
char* http_strcasestr(const char* haystack, const char* needle);

/**
 * Resolve relative path to absolute path
 * If path starts with '/', return as-is (absolute path)
 * If path starts with '~', expand to home directory
 * Otherwise, resolve relative to current working directory
 *
 * @param path The path to resolve
 * @return Resolved absolute path (caller must free), or NULL on error
 */
char* resolve_path(const char* path);

/**
 * Sanitize string to valid UTF-8
 * Replaces invalid UTF-8 bytes with '?'
 *
 * @param input The input string (may contain invalid UTF-8)
 * @return New string with valid UTF-8 (caller must free), or NULL on error
 */
char* sanitize_utf8(const char* input);


char* base64_encode(const unsigned char* data, size_t input_length);

#endif // COMMON_UTILS_H
