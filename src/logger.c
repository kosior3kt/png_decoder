#include "logger.h"

#define LOGGER_MAX_LEVEL_LEN 8

static log_level_e current_log_level;
static FILE* log_file_pointer;



void logger_init(const log_level_e _log_level, const char* _file_path)
{
	ASSERT_AND_FLUSH(_file_path != NULL);
	ASSERT_AND_FLUSH(_log_level < LOG_SIZE);

	log_file_pointer = fopen(_file_path, "w+");
	if(!log_file_pointer) {
		perror("error opening file");
		exit(1);
	}

	current_log_level = _log_level;

	LOG(LOG_INFO, "logging started");
}

void write_simple_log(
		const bool _use_errno,
		const log_level_e _log_level,
		const char* _msg,
		const char* _func,
		...)
{
	ASSERT_AND_FLUSH(log_file_pointer != NULL);
	if(current_log_level < _log_level) {
		return;
	}

	char level[LOGGER_MAX_LEVEL_LEN] = {0};
	switch(_log_level) {
		case LOG_INFO:		strncpy(level, "INFO",	  LOGGER_MAX_LEVEL_LEN); break;
		case LOG_WARNING:	strncpy(level, "WARNING", LOGGER_MAX_LEVEL_LEN); break;
		case LOG_ERROR:		strncpy(level, "ERROR",   LOGGER_MAX_LEVEL_LEN); break;
		case LOG_DEBUG_1:	strncpy(level, "DEBUG_1", LOGGER_MAX_LEVEL_LEN); break;
		case LOG_DEBUG_2:	strncpy(level, "DEBUG_2", LOGGER_MAX_LEVEL_LEN); break;
		case LOG_DEBUG_3:	strncpy(level, "DEBUG_3", LOGGER_MAX_LEVEL_LEN); break;
		case LOG_SIZE:		strncpy(level, "UNKNOWN", LOGGER_MAX_LEVEL_LEN); break;
		case LOG_DEBUG_CRITICAL:	strncpy(level, "TEMP_DEB", LOGGER_MAX_LEVEL_LEN); break;
	}
	ASSERT_AND_FLUSH(memcmp(level, "\0\0\0\0\0\0\0\0", LOGGER_MAX_LEVEL_LEN) != 0);

	fprintf(log_file_pointer, "<%s> [%s]: ", level, _func);
    va_list args;
    va_start(args, _func);
    vfprintf(log_file_pointer, _msg, args);
    va_end(args);
	if(_use_errno) {
		fprintf(log_file_pointer, " ==> errno: %s", strerror(errno));
	}
	fprintf(log_file_pointer, "\n");
}

void change_log_level(const log_level_e _log_level)
{
	current_log_level = _log_level;
}

void logger_close()
{
	ASSERT_AND_FLUSH(log_file_pointer != NULL);
	fflush(log_file_pointer);
	LOG(LOG_INFO, "logging ended");
	fclose(log_file_pointer);
}

void logger_flush()
{
	fflush(log_file_pointer);
}

