#ifndef LOGGER_H
#define LOGGER_H

#include "common.h"

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_ROUTING,
    LOG_LEVEL_NETWORK,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_NONE
} LogLevel;

void logger_init(const char *logfile_path, LogLevel console_level, LogLevel file_level);
void logger_close(void);
void logger_set_console_level(LogLevel level);
void logger_set_file_level(LogLevel level);

typedef void (*LoggerHookFn)(LogLevel level, const char *component, const char *message, const char *timestamp, void *userdata);
void logger_set_hook(LoggerHookFn hook, void *userdata);

void logger_log(LogLevel level, const char *component, const char *fmt, ...);

#define LOG_DEBUG(comp, ...)   logger_log(LOG_LEVEL_DEBUG, comp, __VA_ARGS__)
#define LOG_INFO(comp, ...)    logger_log(LOG_LEVEL_INFO, comp, __VA_ARGS__)
#define LOG_ROUTING(comp, ...) logger_log(LOG_LEVEL_ROUTING, comp, __VA_ARGS__)
#define LOG_NETWORK(comp, ...) logger_log(LOG_LEVEL_NETWORK, comp, __VA_ARGS__)
#define LOG_WARN(comp, ...)    logger_log(LOG_LEVEL_WARN, comp, __VA_ARGS__)
#define LOG_ERROR(comp, ...)   logger_log(LOG_LEVEL_ERROR, comp, __VA_ARGS__)

#endif // LOGGER_H
