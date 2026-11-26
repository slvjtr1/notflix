#pragma once
#include "client.h"
enum { WIDTH = 1280, HEIGHT = 720, SMALL = 0, BODY = 1, TITLE = 2, BRAND = 3 };
bool view_open(SDL_Renderer *renderer, const char *assets);
void view_close(void);
void view_reset_posters(void);
void view_begin(bool video);
void fill(SDL_Rect rect, unsigned rgb, Uint8 alpha);
void label(int x, int y, int max_width, int font, unsigned rgb, const char *text);
void focus_ring(SDL_Rect rect);
void poster(SDL_Rect rect, const char *url);
void art_ready(Request *job);
void view_end(void);
void pairing_code(int x, int y, int size, const char *url);
