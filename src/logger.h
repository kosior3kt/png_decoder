// logger.c

#ifndef __LOGGER__
#define __LOGGER__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <errno.h>

typedef enum{
	LOG_DEBUG_CRITICAL = 0,
	LOG_ERROR,
	LOG_WARNING,
	LOG_INFO,
	LOG_DEBUG_1,
	LOG_DEBUG_2,
	LOG_DEBUG_3,
	LOG_SIZE
} log_level_e;

void logger_init(const log_level_e, const char*);
void logger_close();
void write_simple_log(const bool _use_errno, const log_level_e, const char*, const char*, ...);
void change_log_level(const log_level_e);
void logger_flush();

#define LOG(_log_level, _fmt, args...) write_simple_log(false, _log_level, _fmt, __FUNCTION__, ##args)
#define LOG_ERRNO(_log_level, _fmt, args...) write_simple_log(true, _log_level, _fmt, __FUNCTION__, ##args)
#define ASSERT_AND_FLUSH(_expr) do{logger_flush(); assert(_expr);} while(0);

#endif //__LOGGER__
