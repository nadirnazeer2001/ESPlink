#ifndef DEBUG_H
#define DEBUG_H

#include <Arduino.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

// Log levels
typedef enum {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} log_level_t;

// ANSI color codes (for terminals that support it)
#define ANSI_COLOR_RESET   "\x1b[0m"
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"

// Configuration variables
static log_level_t current_log_level = LOG_LEVEL_DEBUG;
static bool debug_enabled = true;

// =====================
// Configuration functions
// =====================
void debug_set_log_level(log_level_t level) {
    current_log_level = level;
}

log_level_t debug_get_log_level(void) {
    return current_log_level;
}

void debug_set_enabled(bool enabled) {
    debug_enabled = enabled;
}

bool debug_is_enabled(void) {
    return debug_enabled;
}

// =====================
// Internal helpers
// =====================
static void uart_printf(const char *format, ...) {
    if (!debug_enabled) return;

    char buffer[300];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    Serial.print(buffer);
}

static void log_message(const char *color, const char *tag, const char *logstr) {
    if (!debug_enabled) return;

    unsigned long timestamp = millis();
    uart_printf("%s %lu [%s] %s %s\r\n", color, timestamp, tag, logstr, ANSI_COLOR_RESET);
}

// =====================
// Logging functions
// =====================
void LogD(const char *tag, const char *format, ...) {
    if (!debug_enabled || current_log_level > LOG_LEVEL_DEBUG) return;

    va_list args;
    char logstr[255];
    va_start(args, format);
    vsnprintf(logstr, sizeof(logstr), format, args);
    va_end(args);

    log_message(ANSI_COLOR_BLUE, tag, logstr);
}

void LogI(const char *tag, const char *format, ...) {
    if (!debug_enabled || current_log_level > LOG_LEVEL_INFO) return;

    va_list args;
    char logstr[255];
    va_start(args, format);
    vsnprintf(logstr, sizeof(logstr), format, args);
    va_end(args);

    log_message(ANSI_COLOR_GREEN, tag, logstr);
}

void LogW(const char *tag, const char *format, ...) {
    if (!debug_enabled || current_log_level > LOG_LEVEL_WARN) return;

    va_list args;
    char logstr[255];
    va_start(args, format);
    vsnprintf(logstr, sizeof(logstr), format, args);
    va_end(args);

    log_message(ANSI_COLOR_YELLOW, tag, logstr);
}

void LogE(const char *tag, const char *format, ...) {
    if (!debug_enabled) return;

    va_list args;
    char logstr[255];
    va_start(args, format);
    vsnprintf(logstr, sizeof(logstr), format, args);
    va_end(args);

    log_message(ANSI_COLOR_RED, tag, logstr);
}

// =====================
// Byte array printing
// =====================
void byte2HexNbl(char *dest, uint8_t *data, int start, int end, char separator = ' ', int append = 1) {
    const char* pNibbleHex = "0123456789ABCDEF";
    char pBuffer[512] = {0};

    for (int i = start, j = 0; i < end; i++, j++) {
        pBuffer[j*3]     = pNibbleHex[(data[i] >> 4) & 0x0F];
        pBuffer[j*3 + 1] = pNibbleHex[data[i] & 0x0F];
        pBuffer[j*3 + 2] = separator;
    }
    pBuffer[(end-start)*3 - 1] = '\0'; // replace last separator with null

    if (append) {
        strcat(dest, pBuffer);
    } else {
        strcpy(dest, pBuffer);
    }
}

void LogArray(uint8_t *arr, int length, const char *tag, const char *format, ...) {
    if (!debug_enabled || current_log_level > LOG_LEVEL_INFO) return;

    if (length > 200) {
        LogE("DBG", "print array length(%d) avoided to prevent buffer overflow", length);
        return;
    }

    va_list args;
    char logstr[255];
    va_start(args, format);
    vsnprintf(logstr, sizeof(logstr), format, args);
    va_end(args);

    char hexBuffer[600] = {0};
    byte2HexNbl(hexBuffer, arr, 0, length, ' ', 1);

    char finalBuffer[700];
    snprintf(finalBuffer, sizeof(finalBuffer), "%s: %s", logstr, hexBuffer);

    log_message(ANSI_COLOR_GREEN, tag, finalBuffer);
}

#endif // DEBUG_H
