#pragma once

#include <stdio.h>

#ifdef LIBATAFW_DEBUG
#define LIBATAFW_LOG(_fmt, ...)  	printf("[LOG] libatafw: " _fmt, ##__VA_ARGS__)
#define LIBATAFW_ERROR(_fmt, ...)  	printf("[ERROR] libatafw: " _fmt, ##__VA_ARGS__)
#else
#define LIBATAFW_LOG(_fmt, ...)
#define LIBATAFW_ERROR(_fmt, ...)
#endif
