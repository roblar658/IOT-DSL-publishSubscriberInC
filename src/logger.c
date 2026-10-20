#include "logger.h"
#include <time.h>

static FILE *s_log_file = NULL;
static LogLevel s_console_level = LOG_LEVEL_DEBUG;
static LogLevel s_file_level = LOG_LEVEL_DEBUG;
static LoggerHookFn s_hook = NULL;
static void *s_hook_userdata = NULL;

static void lock_logger(void);
static void unlock_logger(void);

void logger_set_hook(LoggerHookFn hook, void *userdata) {
    lock_logger();
    s_hook = hook;
    s_hook_userdata = userdata;
    unlock_logger();
}

#if defined(PLATFORM_WINDOWS)
static CRITICAL_SECTION s_log_lock;
static bool s_lock_inited = false;
static bool s_vt_enabled = false;

static void enable_windows_vt(void) {
    if (s_vt_enabled) return;
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode)) {
            dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, dwMode);
        }
    }
    s_vt_enabled = true;
}
#endif

static void lock_logger(void) {
#if defined(PLATFORM_WINDOWS)
    if (!s_lock_inited) {
        InitializeCriticalSection(&s_log_lock);
        s_lock_inited = true;
    }
    EnterCriticalSection(&s_log_lock);
#endif
}

static void unlock_logger(void) {
#if defined(PLATFORM_WINDOWS)
    LeaveCriticalSection(&s_log_lock);
#endif
}

static const char *level_to_string(LogLevel level) {
    switch (level) {
        case LOG_LEVEL_DEBUG:   return "DEBUG";
        case LOG_LEVEL_INFO:    return "INFO";
        case LOG_LEVEL_ROUTING: return "ROUTING";
        case LOG_LEVEL_NETWORK: return "NETWORK";
        case LOG_LEVEL_WARN:    return "WARN";
        case LOG_LEVEL_ERROR:   return "ERROR";
        default:                return "UNKNOWN";
    }
}

static const char *level_to_color(LogLevel level) {
    switch (level) {
        case LOG_LEVEL_DEBUG:   return "\033[36m"; // Cyan
        case LOG_LEVEL_INFO:    return "\033[32m"; // Green
        case LOG_LEVEL_ROUTING: return "\033[35m"; // Magenta
        case LOG_LEVEL_NETWORK: return "\033[34m"; // Blue
        case LOG_LEVEL_WARN:    return "\033[33m"; // Yellow
        case LOG_LEVEL_ERROR:   return "\033[31;1m"; // Bold Red
        default:                return "\033[0m";
    }
}

static void get_formatted_time(char *buf, size_t max_size) {
    time_t rawtime;
    struct tm *timeinfo;
    time(&rawtime);
#if defined(PLATFORM_WINDOWS)
    struct tm tmbuf;
    localtime_s(&tmbuf, &rawtime);
    timeinfo = &tmbuf;
    SYSTEMTIME st;
    GetLocalTime(&st);
    int ms = (int)(st.wMilliseconds % 1000);
#else
    struct tm tmbuf;
    localtime_r(&rawtime, &tmbuf);
    timeinfo = &tmbuf;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int ms = (int)(tv.tv_usec / 1000) % 1000;
#endif

    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", timeinfo);
    snprintf(buf, max_size, "%s.%03d", time_str, ms);
}

void logger_init(const char *logfile_path, LogLevel console_level, LogLevel file_level) {
    lock_logger();
#if defined(PLATFORM_WINDOWS)
    enable_windows_vt();
#endif

    s_console_level = console_level;
    s_file_level = file_level;

    if (logfile_path && strlen(logfile_path) > 0) {
        if (s_log_file) {
            fclose(s_log_file);
            s_log_file = NULL;
        }
        s_log_file = fopen(logfile_path, "a");
        if (s_log_file) {
            char timebuf[64];
            get_formatted_time(timebuf, sizeof(timebuf));
            fprintf(s_log_file, "\n--- Log session started at %s ---\n", timebuf);
            fflush(s_log_file);
        }
    }
    unlock_logger();
}

void logger_close(void) {
    lock_logger();
    if (s_log_file) {
        fflush(s_log_file);
        fclose(s_log_file);
        s_log_file = NULL;
    }
    unlock_logger();
}

void logger_set_console_level(LogLevel level) {
    lock_logger();
    s_console_level = level;
    unlock_logger();
}

void logger_set_file_level(LogLevel level) {
    lock_logger();
    s_file_level = level;
    unlock_logger();
}

void logger_log(LogLevel level, const char *component, const char *fmt, ...) {
    if (level < s_console_level && (s_log_file == NULL || level < s_file_level)) {
        return;
    }

    char timebuf[96];
    get_formatted_time(timebuf, sizeof(timebuf));

    char msgbuf[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msgbuf, sizeof(msgbuf), fmt, args);
    va_end(args);

    lock_logger();

    const char *lvl_str = level_to_string(level);
    const char *comp = (component && *component) ? component : "CORE";

    // Console output with color
    if (level >= s_console_level) {
        const char *color = level_to_color(level);
        const char *reset = "\033[0m";
        const char *comp_color = "\033[1;37m"; // Bold white
        fprintf(stdout, "[%s] %s%-7s%s %s[%s]%s %s\n",
                timebuf, color, lvl_str, reset, comp_color, comp, reset, msgbuf);
        fflush(stdout);
    }

    // File output (clean, no ANSI escape codes)
    if (s_log_file && level >= s_file_level) {
        fprintf(s_log_file, "[%s] [%-7s] [%s] %s\n",
                timebuf, lvl_str, comp, msgbuf);
        fflush(s_log_file);
    }

    if (s_hook) {
        s_hook(level, comp, msgbuf, timebuf, s_hook_userdata);
    }

    unlock_logger();
}
