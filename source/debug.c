#include "debug.h"

#if DEBUG_LOGGING

#include <switch.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include "config.h"

static FILE *g_log;
static Mutex g_log_lock;
static int g_log_ready;

void debug_init(void) {
  if (g_log_ready) return;
  mutexInit(&g_log_lock);
  g_log_ready = 1;
  g_log = fopen(GAME_HOME "/debug.log", "w");
  if (g_log) setvbuf(g_log, NULL, _IOLBF, 0);
  debug_log("==== %s NX %s debug log ====", GAME_TITLE, GAME_VERSION_NAME);
}

void debug_vlog(const char *fmt, va_list ap) {
  if (!fmt) return;
  char body[2048];
  char line[2200];
  va_list copy;
  va_copy(copy, ap);
  vsnprintf(body, sizeof body, fmt, copy);
  va_end(copy);
  struct timespec ts = {0};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  int n = snprintf(line, sizeof line, "[%5llu.%03llu] %s\n",
                   (unsigned long long)ts.tv_sec,
                   (unsigned long long)(ts.tv_nsec / 1000000), body);
  if (n < 0) return;
  size_t len = (size_t)n < sizeof line ? (size_t)n : sizeof line - 1;
  if (g_log_ready) mutexLock(&g_log_lock);
  /* Do not write persistent diagnostics through stdout.  stdout remains bound
   * to libnx's software console after consoleExit(), whose renderer has already
   * been destroyed; using it then dereferences a null draw callback.  Startup
   * status text is printed directly while the console is alive, while logs
   * continue to the SD file and Atmosphere's debug channel. */
  if (g_log) { fwrite(line, 1, len, g_log); fflush(g_log); }
  svcOutputDebugString(line, len);
  if (g_log_ready) mutexUnlock(&g_log_lock);
}

void debug_log(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  debug_vlog(fmt, ap);
  va_end(ap);
}

void debug_shutdown(void) {
  if (!g_log_ready) return;
  debug_log("debug log closed");
  mutexLock(&g_log_lock);
  if (g_log) fclose(g_log);
  g_log = NULL;
  mutexUnlock(&g_log_lock);
}

#endif
