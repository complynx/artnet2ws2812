#ifndef LOGGER_H_
#define LOGGER_H_

#define LOGGER_NONE 0   /*!< No log output */
#define LOGGER_ERROR 1      /*!< Critical errors, software module can not recover on its own */
#define LOGGER_WARN 2       /*!< Error conditions from which recovery measures have been taken */
#define LOGGER_INFO 3       /*!< Information messages which describe normal flow of events */
#define LOGGER_DEBUG 4      /*!< Extra information which is not necessary for normal use (values, pointers, sizes, etc). */
#define LOGGER_VERBOSE 5    /*!< Bigger chunks of debugging information, or frequent messages which can potentially flood the output. */


#ifndef LOGGER_LEVEL
#define LOGGER_LEVEL LOGGER_INFO
#endif

#ifndef LOGGER_COLORS
#define LOGGER_COLORS 1
#endif

#if LOGGER_COLORS == 1
#define LOG_COLOR_BLACK   "30"
#define LOG_COLOR_RED     "31"
#define LOG_COLOR_GREEN   "32"
#define LOG_COLOR_BROWN   "33"
#define LOG_COLOR_BLUE    "34"
#define LOG_COLOR_PURPLE  "35"
#define LOG_COLOR_CYAN    "36"
#define LOG_COLOR(COLOR)  "\033[0;" COLOR "m"
#define LOG_BOLD(COLOR)   "\033[1;" COLOR "m"
#define LOG_RESET_COLOR   "\033[0m"
#define LOG_COLOR_E       LOG_COLOR(LOG_COLOR_RED)
#define LOG_COLOR_W       LOG_COLOR(LOG_COLOR_BROWN)
#define LOG_COLOR_I       LOG_COLOR(LOG_COLOR_GREEN)
#define LOG_COLOR_D
#define LOG_COLOR_V
#else //CONFIG_LOG_COLORS
#define LOG_COLOR_E
#define LOG_COLOR_W
#define LOG_COLOR_I
#define LOG_COLOR_D
#define LOG_COLOR_V
#define LOG_RESET_COLOR
#endif //CONFIG_LOG_COLORS


#define LOGGER_EOL "\n"

void logger_log_r(const char* file, int line, int level, const char* tag, int no_eol, const char* format, ... ) __attribute__ ((format (printf, 6, 7)));

#define LOG_R(LVL, TAG, FMT, ...) logger_log_r(__FILE__, __LINE__, LVL, TAG, 0, FMT, ##__VA_ARGS__)
#define LOG_RN(LVL, TAG, FMT, ...) logger_log_r(__FILE__, __LINE__, LVL, TAG, 1, FMT, ##__VA_ARGS__)

#if LOGGER_LEVEL >= LOGGER_ERROR
#define LOGE(FMT, ...) LOG_R(LOGGER_ERROR, TAG, FMT, ##__VA_ARGS__)
#define LOGE_N(FMT, ...) LOG_RN(LOGGER_ERROR, TAG, FMT, ##__VA_ARGS__)
#else
#define LOGE(FMT, ...) {}
#define LOGE_N(FMT, ...) {}
#endif

#if LOGGER_LEVEL >= LOGGER_WARN
#define LOGW(FMT, ...) LOG_R(LOGGER_WARN, TAG, FMT, ##__VA_ARGS__)
#define LOGW_N(FMT, ...) LOG_RN(LOGGER_WARN, TAG, FMT, ##__VA_ARGS__)
#else
#define LOGW(FMT, ...) {}
#define LOGW_N(FMT, ...) {}
#endif

#if LOGGER_LEVEL >= LOGGER_INFO
#define LOGI(FMT, ...) LOG_R(LOGGER_INFO, TAG, FMT, ##__VA_ARGS__)
#define LOGI_N(FMT, ...) LOG_RN(LOGGER_INFO, TAG, FMT, ##__VA_ARGS__)
#else
#define LOGI(FMT, ...) {}
#define LOGI_N(FMT, ...) {}
#endif

#if LOGGER_LEVEL >= LOGGER_DEBUG
#define LOGD(FMT, ...) LOG_R(LOGGER_DEBUG, TAG, FMT, ##__VA_ARGS__)
#define LOGD_N(FMT, ...) LOG_RN(LOGGER_DEBUG, TAG, FMT, ##__VA_ARGS__)
#else
#define LOGD(FMT, ...) {}
#define LOGD_N(FMT, ...) {}
#endif

#if LOGGER_LEVEL >= LOGGER_VERBOSE
#define LOGV(FMT, ...) LOG_R(LOGGER_VERBOSE, TAG, FMT, ##__VA_ARGS__)
#define LOGV_N(FMT, ...) LOG_RN(LOGGER_VERBOSE, TAG, FMT, ##__VA_ARGS__)
#else
#define LOGV(FMT, ...) {}
#define LOGV_N(FMT, ...) {}
#endif

#endif /* LOGGER_H_ */
