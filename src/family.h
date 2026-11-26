#pragma once
#include "catalog.h"
bool kids_active(const cJSON *person);
bool allowed_title(const cJSON *person, const cJSON *title);
bool pin_set(cJSON *settings, const char *pin);
bool pin_matches(const cJSON *settings, const char *pin);
