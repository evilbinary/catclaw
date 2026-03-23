/**
 * Common utility functions implementation
 */
#include "utils.h"
#include <ctype.h>

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
