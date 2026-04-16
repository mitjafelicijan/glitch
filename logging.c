#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/time.h>

#include "glitch.h"

static LogLevel max_level = LOG_INFO;

static const char* level_strings[] = {
	"ERROR",
	"WARN",
	"INFO",
	"DEBUG",
};

static const char* level_colors[] = {
	COLOR_ERROR,
	COLOR_WARNING,
	COLOR_INFO,
	COLOR_DEBUG,
};

void set_log_level(LogLevel level) {
	max_level = level;
}

LogLevel get_log_level_from_env(void) {
	const char *env = getenv("LOG_LEVEL");
	if (env) {
		int level = atoi(env);
		if (level >= 0 && level <= 3) {
			return (LogLevel)level;
		}
	}

	return max_level;
}

void log_message(FILE *stream, LogLevel level, const char* format, ...) {
	if (max_level < level) return;

	struct timeval tv;
	gettimeofday(&tv, NULL);
	struct tm* tm_info = localtime(&tv.tv_sec);

	char time_str[24];
	strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

	const char *color = isatty(fileno(stream)) ? level_colors[level] : "";
	const char *reset = isatty(fileno(stream)) ? COLOR_RESET : "";

	const char* log_format = "%s[%s.%03d] [%-5s] ";
	fprintf(stream, log_format,
			color,
			time_str,
			(int)(tv.tv_usec / 1000),
			level_strings[level]);

	va_list args;
	va_start(args, format);
	vfprintf(stream, format, args);
	va_end(args);

	fprintf(stream, "%s\n", reset);
	fflush(stream);
}
