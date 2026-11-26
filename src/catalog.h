#pragma once
#include "cJSON.h"
#include <stddef.h>
#include <stdbool.h>

const char *field(const cJSON *obj, const char *key);
bool web_url(const char *url);
char *resource_url(const char *manifest, const char *resource, const char *type, const char *id,
                   const char *search);
bool has_resource(const cJSON *manifest, const char *resource, const char *type);
bool searchable(const cJSON *catalog);
bool plain_stream(const cJSON *stream);
cJSON *read_json(const char *path);
bool save_json(const char *path, const cJSON *value);
void put_string(cJSON *obj, const char *key, const char *value);
void put_number(cJSON *obj, const char *key, double value);
cJSON *array_field(cJSON *obj, const char *key);
int find_title(const cJSON *list, const char *type, const char *id);
