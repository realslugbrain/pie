#ifndef PIE_RUNTIME_H
#define PIE_RUNTIME_H

#include <stdbool.h>

const char *pie_runtime_version(void);
bool pie_maybe(void);
void pie_runtime_keep_linker(void);

#endif
