#pragma once
#include <SDL.h>
#include "catalog.h"
typedef enum { JSON_JOB, ART_JOB, DOWNLOAD_JOB, CAPTION_JOB } JobType;
typedef struct Request {
    struct Request *next;
    JobType type;
    unsigned owner;
    char *url, *path, *body, *api_key, *bearer;
    int operation;
    char error[120];
    cJSON *json;
    char *text;
    SDL_Surface *image;
} Request;
bool client_start(const char *ca);
bool request(JobType type, unsigned owner, const char *url, const char *path);
bool nuvio_request(unsigned owner,int operation,const char *endpoint,const char *key,const char *token,const cJSON *body);
Request *completed(void);
void request_free(Request *job);
void client_stop(void);
