#ifndef ZF_DEBUG_H
#define ZF_DEBUG_H

#include <stdarg.h>

#ifndef DEBUG_LOGGING
#define DEBUG_LOGGING 0
#endif

#if DEBUG_LOGGING
void debug_init(void);
void debug_shutdown(void);
void debug_vlog(const char *fmt, va_list ap);
void debug_log(const char *fmt, ...) __attribute__((format(printf,1,2)));
#else
#define debug_init() ((void)0)
#define debug_shutdown() ((void)0)
#define debug_vlog(...) ((void)0)
#define debug_log(...) ((void)0)
#endif

#endif
