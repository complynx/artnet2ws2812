#include "logger.h"
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

static const char* level_str(int level) {
    switch(level){
    case LOGGER_VERBOSE:
        return "V";
    case LOGGER_DEBUG:
        return "D";
    case LOGGER_INFO:
        return "I";
    case LOGGER_WARN:
        return "W";
    case LOGGER_ERROR:
        return "E";
    default:
        return "L";
    }
}

#if LOGGER_COLORS == 1
static const char* level_color(int level) {
    switch(level){
    case LOGGER_VERBOSE:
        return LOG_COLOR_V"";
    case LOGGER_DEBUG:
        return LOG_COLOR_D"";
    case LOGGER_INFO:
        return LOG_COLOR_I"";
    case LOGGER_WARN:
        return LOG_COLOR_W"";
    case LOGGER_ERROR:
        return LOG_COLOR_E"";
    default:
        return "";
    }
}
#endif

void logger_log_r(const char* file, int line, int level, const char* tag, int no_eol, const char* format, ... ) {
    va_list va;
#if LOGGER_COLORS == 1
    printf(level_color(level));
#endif
    printf("%s| [%s] %s:%d ", level_str(level), tag?tag:"NULL", file, line);

    va_start(va, format);
    vprintf(format, va);
    va_end(va);

#if LOGGER_COLORS == 1
    if(no_eol) printf(LOG_RESET_COLOR"");
    else printf(LOG_RESET_COLOR LOGGER_EOL);
#else
    if(!no_eol) printf(LOGGER_EOL);
#endif
}

