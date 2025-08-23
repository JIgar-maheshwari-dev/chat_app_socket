#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>

#define LOG_LEVEL_NONE  0
#define LOG_LEVEL_ERROR 1
#define LOG_LEVEL_INFO  2
#define LOG_LEVEL_DEBUG 3

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_NONE
#endif

#define COLOR_BOLD_RED     "\033[1;31m"
#define COLOR_BOLD_GREEN   "\033[1;32m"
#define COLOR_BOLD_YELLOW  "\033[1;33m"
#define COLOR_RESET        "\033[0m"

extern FILE *log_fp;

#if LOG_LEVEL>=LOG_LEVEL_ERROR
    #define LOGE(fmt, ...) fprintf(stderr, COLOR_BOLD_RED "[ ERROR ] " COLOR_RESET "%s:%d: "  fmt "\n",\
                            __func__, __LINE__, ##__VA_ARGS__)
#else
    #define LOGE(fmt,...)
#endif

#if LOG_LEVEL>=LOG_LEVEL_INFO
    #define LOGI(fmt, ...) fprintf(stderr, COLOR_BOLD_GREEN "[ INFO ]  " COLOR_RESET "%s:%d: " fmt "\n",\
                            __func__, __LINE__, ##__VA_ARGS__)
#else
    #define LOGI(fmt,...)
#endif

#if LOG_LEVEL>=LOG_LEVEL_DEBUG
    #define LOGD(fmt, ...) fprintf(stderr, COLOR_BOLD_YELLOW "[ DEBUG ] "COLOR_RESET "%s:%d: " fmt "\n",\
                            __func__, __LINE__, ##__VA_ARGS__)
#else
    #define LOGD(fmt,...)
#endif

#endif