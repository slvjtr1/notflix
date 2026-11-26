#pragma once
#include <stdbool.h>
bool captions_load(char *text);
const char *caption_at(double seconds);
void captions_clear(void);
