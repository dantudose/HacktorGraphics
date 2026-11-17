#pragma once

// Set HACKTOR_LOG_LEVEL in platformio.ini. Disabled messages and their argument
// expressions are removed by the preprocessor, including their format strings.
#define WATCH_LOG_LEVEL_OFF 0
#define WATCH_LOG_LEVEL_ERROR 1
#define WATCH_LOG_LEVEL_WARNING 2
#define WATCH_LOG_LEVEL_INFO 3
#define WATCH_LOG_LEVEL_DEBUG 4
#define WATCH_LOG_LEVEL_VERBOSE 5

#ifndef HACKTOR_LOG_LEVEL
#define HACKTOR_LOG_LEVEL WATCH_LOG_LEVEL_OFF
#endif

#if HACKTOR_LOG_LEVEL < WATCH_LOG_LEVEL_OFF || HACKTOR_LOG_LEVEL > WATCH_LOG_LEVEL_VERBOSE
#error "HACKTOR_LOG_LEVEL must be between 0 (off) and 5 (verbose)"
#endif

#if HACKTOR_LOG_LEVEL > WATCH_LOG_LEVEL_OFF
#include <Arduino.h>

// The first variadic argument must be a format string literal. Keep the macro
// a single statement so callers may safely use it in an unbraced if/else.
#define WATCH_LOG_EMIT_(prefix, ...) \
  do {                              \
    Serial.printf(prefix __VA_ARGS__); \
    Serial.println();               \
  } while (0)
#endif

#if HACKTOR_LOG_LEVEL >= WATCH_LOG_LEVEL_ERROR
#define WATCH_LOG_E(...) WATCH_LOG_EMIT_("[E] ", __VA_ARGS__)
#else
#define WATCH_LOG_E(...) do {} while (0)
#endif

#if HACKTOR_LOG_LEVEL >= WATCH_LOG_LEVEL_WARNING
#define WATCH_LOG_W(...) WATCH_LOG_EMIT_("[W] ", __VA_ARGS__)
#else
#define WATCH_LOG_W(...) do {} while (0)
#endif

#if HACKTOR_LOG_LEVEL >= WATCH_LOG_LEVEL_INFO
#define WATCH_LOG_I(...) WATCH_LOG_EMIT_("[I] ", __VA_ARGS__)
#else
#define WATCH_LOG_I(...) do {} while (0)
#endif

#if HACKTOR_LOG_LEVEL >= WATCH_LOG_LEVEL_DEBUG
#define WATCH_LOG_D(...) WATCH_LOG_EMIT_("[D] ", __VA_ARGS__)
#else
#define WATCH_LOG_D(...) do {} while (0)
#endif

#if HACKTOR_LOG_LEVEL >= WATCH_LOG_LEVEL_VERBOSE
#define WATCH_LOG_V(...) WATCH_LOG_EMIT_("[V] ", __VA_ARGS__)
#else
#define WATCH_LOG_V(...) do {} while (0)
#endif
