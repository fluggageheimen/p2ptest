#pragma once
#include <stdio.h>

static int log_consolemax = 0;
static int log_filemin = -1;
static int log_filemax = -1;

void log(int lvl, char const* fmt, ...);
