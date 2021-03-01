#include "log.h"

#include <stdio.h>
#include <stdarg.h>


// TODO: write todo
void log(int lvl, char const* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
	printf("\n");
}