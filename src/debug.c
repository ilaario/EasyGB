#include "include/debug.h"

#ifdef DEBUGLOG

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static bool dbg_is_initialized = false;
static bool dbg_is_enabled = false;
static bool dbg_is_mem_enabled = false;
static bool dbg_flush_each_line = true;
static bool dbg_owns_stream = false;
static FILE *dbg_stream = NULL;
static uint64_t dbg_step_counter = 0;

static bool parse_bool_env(const char *name, bool default_value) {
    const char *value = getenv(name);
    if (value == NULL || value[0] == '\0') {
        return default_value;
    }

    if (strcmp(value, "0") == 0 ||
        strcasecmp(value, "false") == 0 ||
        strcasecmp(value, "off") == 0 ||
        strcasecmp(value, "no") == 0) {
        return false;
    }

    if (strcmp(value, "1") == 0 ||
        strcasecmp(value, "true") == 0 ||
        strcasecmp(value, "on") == 0 ||
        strcasecmp(value, "yes") == 0) {
        return true;
    }

    return default_value;
}

static void dbg_init_once(void) {
    if (dbg_is_initialized) {
        return;
    }

    dbg_is_initialized = true;
    dbg_is_enabled = parse_bool_env("EASYGB_LOG", false);
    dbg_is_mem_enabled = parse_bool_env("EASYGB_LOG_MEM", false);
    dbg_flush_each_line = parse_bool_env("EASYGB_LOG_FLUSH", true);

    dbg_stream = stderr;
    const char *path = getenv("EASYGB_LOG_FILE");
    if (path != NULL && path[0] != '\0') {
        FILE *f = fopen(path, "w");
        if (f != NULL) {
            dbg_stream = f;
            dbg_owns_stream = true;
        } else {
            fprintf(stderr, "[DBG] Failed to open EASYGB_LOG_FILE='%s', using stderr\n", path);
        }
    }

    if (dbg_is_enabled) {
        time_t now = time(NULL);
        fprintf(dbg_stream, "[DBG] logging enabled at %ld\n", (long)now);
        if (dbg_flush_each_line) {
            fflush(dbg_stream);
        }
    }
}

static void dbg_vlog(const char *tag, const char *fmt, va_list args) {
    dbg_init_once();
    if (!dbg_is_enabled || dbg_stream == NULL) {
        return;
    }

    fprintf(dbg_stream, "%s ", tag);
    vfprintf(dbg_stream, fmt, args);
    fputc('\n', dbg_stream);

    if (dbg_flush_each_line) {
        fflush(dbg_stream);
    }
}

void dbg_init(void) {
    dbg_init_once();
}

void dbg_shutdown(void) {
    if (!dbg_is_initialized) {
        return;
    }

    if (dbg_owns_stream && dbg_stream != NULL) {
        fclose(dbg_stream);
    }

    dbg_stream = NULL;
    dbg_owns_stream = false;
    dbg_is_initialized = false;
}

bool dbg_enabled(void) {
    dbg_init_once();
    return dbg_is_enabled;
}

bool dbg_mem_enabled(void) {
    dbg_init_once();
    return dbg_is_mem_enabled;
}

uint64_t dbg_next_step(void) {
    dbg_init_once();
    dbg_step_counter++;
    return dbg_step_counter;
}

void dbg_log(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    dbg_vlog("[DBG]", fmt, args);
    va_end(args);
}

void dbg_log_mem(const char *fmt, ...) {
    dbg_init_once();
    if (!dbg_is_enabled || !dbg_is_mem_enabled) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    dbg_vlog("[MEM]", fmt, args);
    va_end(args);
}

#else

void dbg_init(void) {}
void dbg_shutdown(void) {}
bool dbg_enabled(void) { return false; }
bool dbg_mem_enabled(void) { return false; }
uint64_t dbg_next_step(void) { return 0; }
void dbg_log(const char *fmt, ...) { (void)fmt; }
void dbg_log_mem(const char *fmt, ...) { (void)fmt; }

#endif
