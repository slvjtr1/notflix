#pragma once
#include <SDL.h>
#include "catalog.h"
typedef enum { JSON_JOB, ART_JOB, DOWNLOAD_JOB } JobType;
typedef struct Request {
    struct Request *next;
    JobType type;
    unsigned owner;
    char *url, *path;
    char error[120];
    cJSON *json;
    SDL_Surface *image;
} Request;
bool client_start(const char *ca);
bool request(JobType type, unsigned owner, const char *url, const char *path);
Request *completed(void);
void request_free(Request *job);
void client_stop(void);
