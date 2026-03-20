#ifndef WORKSPACE_H
#define WORKSPACE_H

#include <stdbool.h>

// Functions
bool workspace_init(const char* workspace_path);
void workspace_cleanup(void);
char* workspace_get_path(void);

#endif // WORKSPACE_H