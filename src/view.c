#include "view.h"
#include <SDL_ttf.h>
#include "qrcodegen.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static SDL_Renderer *ren;
static TTF_Font *fonts[4];
static SDL_Texture *glow, *qr_texture;
static char qr_link[1024];
static unsigned frame;
typedef struct {
    char *key;
    unsigned used, color;
    int font, w, h;
    SDL_Texture *tex;
} Text;
typedef struct {
    char *url;
    unsigned used, retry;
    bool pending;
    SDL_Texture *tex;
} Art;
static Text labels[128];
static Art images[36];

static SDL_Color color(unsigned rgb, Uint8 a) {
    return (SDL_Color){(Uint8)(rgb >> 16), (Uint8)(rgb >> 8), (Uint8)rgb, a};
}
bool view_open(SDL_Renderer *r, const char *assets) {
    ren = r;
    int sizes[] = {18, 25, 46, 62};
    char path[1024];
    for (int i = 0; i < 4; i++) {
        snprintf(path, sizeof path, "%s/%s.ttf", assets, i == BRAND ? "brand" : "interface");
        fonts[i] = TTF_OpenFont(path, sizes[i]);
        if (!fonts[i])
            return false;
    }
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, 64, 64, 32, SDL_PIXELFORMAT_RGBA32);
    if (!s)
        return false;
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            int dx = x < 15   ? 15 - x
                     : x > 48 ? x - 48
                              : 0,
                dy = y < 15   ? 15 - y
                     : y > 48 ? y - 48
                              : 0;
            float d = sqrtf((float)(dx * dx + dy * dy));
            int edge = SDL_min(SDL_min(x - 15, 48 - x), SDL_min(y - 15, 48 - y));
            Uint8 a = edge >= 2 ? 0 : d < 1.8 ? 255 : (Uint8)(115 * expf(-0.27f * d));
            ((Uint32 *)((char *)s->pixels + y * s->pitch))[x] =
                SDL_MapRGBA(s->format, 255, 255, 255, a);
        }
    glow = SDL_CreateTextureFromSurface(r, s);
    SDL_FreeSurface(s);
    if (!glow)
        return false;
    SDL_SetTextureBlendMode(glow, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    return true;
}
void fill(SDL_Rect rect, unsigned rgb, Uint8 alpha) {
    SDL_Color c = color(rgb, alpha);
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(ren, &rect);
}
void view_begin(bool video) {
    frame++;
    SDL_SetRenderDrawColor(ren, video ? 0 : 15, video ? 0 : 15, video ? 0 : 19, video ? 0 : 255);
    SDL_RenderClear(ren);
}
void label(int x, int y, int max_width, int font, unsigned rgb, const char *text) {
    if (!text || !*text || max_width < 1)
        return;
    Text *t = NULL, *old = &labels[0];
    for (size_t i = 0; i < SDL_arraysize(labels); i++) {
        Text *v = &labels[i];
        if (v->key && v->font == font && v->color == rgb && !strcmp(v->key, text)) {
            t = v;
            break;
        }
        if (!v->key || v->used < old->used)
            old = v;
    }
    if (!t) {
        t = old;
        free(t->key);
        SDL_DestroyTexture(t->tex);
        *t = (Text){0};
        SDL_Surface *s = TTF_RenderUTF8_Blended(fonts[font], text, color(rgb, 255));
        if (!s)
            return;
        t->tex = SDL_CreateTextureFromSurface(ren, s);
        t->w = s->w;
        t->h = s->h;
        SDL_FreeSurface(s);
        t->key = strdup(text);
        t->font = font;
        t->color = rgb;
    }
    t->used = frame;
    SDL_Rect src = {0, 0, SDL_min(max_width, t->w), t->h}, dst = {x, y, src.w, src.h};
    if (t->tex)
        SDL_RenderCopy(ren, t->tex, &src, &dst);
}
void focus_ring(SDL_Rect r) {
    int sx[] = {0, 20, 44, 64}, sy[] = {0, 20, 44, 64};
    int xx[] = {r.x - 15, r.x + 5, r.x + r.w - 5, r.x + r.w + 15};
    int yy[] = {r.y - 15, r.y + 5, r.y + r.h - 5, r.y + r.h + 15};
    for (int y = 0; y < 3; y++)
        for (int x = 0; x < 3; x++)
            if (x != 1 || y != 1) {
                SDL_Rect src = {sx[x], sy[y], sx[x + 1] - sx[x], sy[y + 1] - sy[y]};
                SDL_Rect dst = {xx[x], yy[y], xx[x + 1] - xx[x], yy[y + 1] - yy[y]};
                SDL_RenderCopy(ren, glow, &src, &dst);
            }
}
void poster(SDL_Rect r, const char *url) {
    fill(r, 0x292930, 255);
    if (!web_url(url))
        return;
    Art *a = NULL, *old = NULL;
    int pending = 0;
    for (size_t i = 0; i < SDL_arraysize(images); i++) {
        Art *v = &images[i];
        if (v->pending)
            pending++;
        if (v->url && !strcmp(v->url, url))
            a = v;
        if (!v->pending && (!old || !v->url || v->used < old->used))
            old = v;
    }
    if (!a && old && pending < 5) {
        a = old;
        free(a->url);
        SDL_DestroyTexture(a->tex);
        *a = (Art){.url = strdup(url)};
    }
    if (!a)
        return;
    a->used = frame;
    if (!a->tex && !a->pending && pending < 5 && SDL_TICKS_PASSED(SDL_GetTicks(), a->retry))
        a->pending = request(ART_JOB, 0, url, NULL);
    if (a->tex)
        SDL_RenderCopy(ren, a->tex, NULL, &r);
}
void art_ready(Request *r) {
    for (size_t i = 0; i < SDL_arraysize(images); i++) {
        Art *a = &images[i];
        if (!a->url || strcmp(a->url, r->url))
            continue;
        a->pending = false;
        if (r->image)
            a->tex = SDL_CreateTextureFromSurface(ren, r->image);
        a->retry = SDL_GetTicks() + 60000;
        return;
    }
}
void pairing_code(int x, int y, int size, const char *url) {
    if (!url || !*url || strlen(url) >= sizeof qr_link)
        return;
    if (strcmp(url, qr_link)) {
        SDL_DestroyTexture(qr_texture);
        qr_texture = NULL;
        snprintf(qr_link, sizeof qr_link, "%s", url);
        uint8_t bits[qrcodegen_BUFFER_LEN_FOR_VERSION(12)], temp[sizeof bits];
        if (!qrcodegen_encodeText(url, temp, bits, qrcodegen_Ecc_MEDIUM, 1, 12, qrcodegen_Mask_AUTO,
                                  true))
            return;
        int dim = qrcodegen_getSize(bits);
        SDL_Surface *s =
            SDL_CreateRGBSurfaceWithFormat(0, dim + 8, dim + 8, 32, SDL_PIXELFORMAT_RGBA32);
        if (!s)
            return;
        SDL_FillRect(s, NULL, SDL_MapRGBA(s->format, 255, 255, 255, 255));
        for (int j = 0; j < dim; j++)
            for (int i = 0; i < dim; i++)
                if (qrcodegen_getModule(bits, i, j)) {
                    SDL_Rect p = {i + 4, j + 4, 1, 1};
                    SDL_FillRect(s, &p, SDL_MapRGBA(s->format, 0, 0, 0, 255));
                }
        qr_texture = SDL_CreateTextureFromSurface(ren, s);
        SDL_FreeSurface(s);
    }
    SDL_Rect dst = {x, y, size, size};
    if (qr_texture)
        SDL_RenderCopy(ren, qr_texture, NULL, &dst);
}
void view_end(void) { SDL_RenderPresent(ren); }
void view_reset_posters(void) {
    for (size_t i = 0; i < SDL_arraysize(images); i++) {
        Art *a = &images[i];
        if (!a->pending) {
            free(a->url);
            SDL_DestroyTexture(a->tex);
            *a = (Art){0};
        }
    }
}
void view_close(void) {
    for (size_t i = 0; i < SDL_arraysize(labels); i++) {
        free(labels[i].key);
        SDL_DestroyTexture(labels[i].tex);
    }
    for (size_t i = 0; i < SDL_arraysize(images); i++) {
        free(images[i].url);
        SDL_DestroyTexture(images[i].tex);
    }
    SDL_DestroyTexture(glow);
    SDL_DestroyTexture(qr_texture);
    for (int i = 0; i < 4; i++)
        if (fonts[i])
            TTF_CloseFont(fonts[i]);
}
