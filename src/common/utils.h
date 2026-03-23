/**
 * Common utility functions
 */
#ifndef COMMON_UTILS_H
#define COMMON_UTILS_H

#include <stddef.h>

/**
 * Case-insensitive string search
 * Similar to strstr() but ignores case
 * 
 * @param haystack The string to search in
 * @param needle The substring to search for
 * @return Pointer to the first occurrence, or NULL if not found
 */
char* http_strcasestr(const char* haystack, const char* needle);

#endif // COMMON_UTILS_H
