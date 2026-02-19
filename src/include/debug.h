#ifndef DEBUG_H
#define DEBUG_H

#include <stdbool.h>
#include <stdint.h>

void dbg_init(void);
void dbg_shutdown(void);
bool dbg_enabled(void);
bool dbg_mem_enabled(void);
uint64_t dbg_next_step(void);
void dbg_log(const char *fmt, ...);
void dbg_log_mem(const char *fmt, ...);

#endif
