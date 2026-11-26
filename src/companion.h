#pragma once
#include <stdbool.h>
bool companion_start(char *link, int cap);
void companion_stop(void);
void companion_tick(void);
void companion_input(const char *route, const char *value, const char *type);
