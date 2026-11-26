#include "app.h"
#include "companion.h"
#include "bench.h"
#include "platform/plat.h"
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#ifdef WEBOS
#include <SDL2/SDL_webOS.h>
#endif

bool running = true, test_mode;
char assets[896], data_dir[896], state_path[1024];
static SDL_Window *window;
static SDL_Renderer *renderer;
static FILE *diagnostics;
static SDL_atomic_t background;
volatile int saver_tv_hold;
int app_behind(void) { return SDL_AtomicGet(&background); }
const char *plat_device_name(void) { return "LG webOS TV"; }

static bool setup_paths(void) {
#ifdef WEBOS
    char exe[880];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0)
        return false;
    exe[n] = 0;
    char *slash = strrchr(exe, '/');
    if (!slash)
        return false;
    *slash = 0;
    snprintf(assets, sizeof assets, "%s/assets", exe);
#else
    if (!getcwd(assets, sizeof assets - 16))
        return false;
    strcat(assets, "/assets");
#endif
    if (test_mode) {
        snprintf(data_dir, sizeof data_dir, "/tmp");
    } else {
        const char *dir = getenv("NOTFLIX_DATA");
#ifdef WEBOS
        const char *choices[] = {dir, "/media/developer/apps/usr/palm/data/tv.notflix.native",
                                 "/media/developer/.tv.notflix.native",
                                 "/media/internal/.tv.notflix.native"};
#else
        char *pref = SDL_GetPrefPath("Notflix", "tv.notflix.native");
        const char *choices[] = {dir, pref};
#endif
        for (unsigned i = 0; i < SDL_arraysize(choices); i++) {
            if (!choices[i] || strlen(choices[i]) >= sizeof data_dir)
                continue;
            mkdir(choices[i], 0700);
            if (!access(choices[i], W_OK | X_OK)) {
                snprintf(data_dir, sizeof data_dir, "%s", choices[i]);
                break;
            }
        }
#ifndef WEBOS
        SDL_free(pref);
#endif
        if (!data_dir[0])
            return false;
    }
    snprintf(state_path, sizeof state_path, "%s/state.json", data_dir);
    return true;
}
static void events(SDL_Event *e) {
    Screen *s = current();
    if (e->type == SDL_QUIT) {
        running = false;
        return;
    }
    if (e->type == SDL_WINDOWEVENT) {
        if (e->window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
            SDL_AtomicSet(&background, 1);
            if (s->page == PLAYER) {
                capture_progress();
                video_pause();
            }
        }
        if (e->window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
            SDL_AtomicSet(&background, 0);
        return;
    }
    if (e->type == SDL_TEXTINPUT) {
        type_text(e->text.text);
        return;
    }
    if (e->type == SDL_MOUSEMOTION ||
        (e->type == SDL_MOUSEBUTTONUP && e->button.button == SDL_BUTTON_LEFT)) {
        int x = e->type == SDL_MOUSEMOTION ? e->motion.x : e->button.x,
            y = e->type == SDL_MOUSEMOTION ? e->motion.y : e->button.y;
        if (s->page == PLAYER)
            controls_wake();
        hit(x, y, e->type == SDL_MOUSEBUTTONUP);
        return;
    }
    if (e->type != SDL_KEYDOWN)
        return;
    SDL_Keycode k = e->key.keysym.sym;
#ifdef WEBOS
    if (e->key.keysym.scancode == SDL_WEBOS_SCANCODE_BACK)
        k = SDLK_ESCAPE;
    if (e->key.keysym.scancode == SDL_WEBOS_SCANCODE_EXIT) {
        running = false;
        return;
    }
#endif
    if (s->page == PLAYER) {
        bool visible = controls_visible();
        controls_wake();
        if (!visible && (k == SDLK_RETURN || k == SDLK_SELECT))
            return;
    }
    if (bench_active()) {
        if (k == SDLK_ESCAPE || k == SDLK_AC_BACK)
            bench_cancel();
        return;
    }
    switch (k) {
    case SDLK_UP:
        move_focus(0, -1);
        break;
    case SDLK_DOWN:
        move_focus(0, 1);
        break;
    case SDLK_LEFT:
        move_focus(-1, 0);
        break;
    case SDLK_RIGHT:
        move_focus(1, 0);
        break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
    case SDLK_SELECT:
        if (!e->key.repeat)
            activate();
        break;
    case SDLK_ESCAPE:
    case SDLK_AC_BACK:
        if (!e->key.repeat)
            back();
        break;
    case SDLK_BACKSPACE:
        if (s->page == KEYBOARD)
            erase_text();
        else
            back();
        break;
    case SDLK_AUDIOPLAY:
        if (s->page == PLAYER) {
            if (video_state() == VID_PAUSED)
                video_play();
            else
                video_pause();
        }
        break;
    case SDLK_AUDIOSTOP:
        if (s->page == PLAYER)
            back();
        break;
    default:
        break;
    }
}
static bool wait_for_jobs(void) {
    Uint32 start = SDL_GetTicks();
    while (current()->pending && SDL_GetTicks() - start < 8000) {
        Request *r;
        while ((r = completed())) {
            handle_reply(r);
            request_free(r);
        }
        SDL_Delay(2);
    }
    return !current()->pending;
}
static bool fixture_test(const char *manifest) {
    cJSON_DeleteItemFromObject(db, "addons");
    array_field(db, "addons");
    current()->focus = 6;
    activate();
    activate();
    activate();
    if (current()->page != KEYBOARD)
        return false;
    type_text(manifest);
    current()->focus = cJSON_GetArraySize(current()->rows) - 1;
    activate();
    if (!wait_for_jobs() || cJSON_GetArraySize(array_field(db, "addons")) != 1)
        return false;
    while (depth)
        back();
    current()->focus = 0;
    activate();
    activate();
    if (!wait_for_jobs() || current()->page != TITLES || cJSON_GetArraySize(current()->rows) != 1)
        return false;
    activate();
    if (!wait_for_jobs() || current()->page != DETAIL)
        return false;
    current()->focus = 1;
    activate();
    if (cJSON_GetArraySize(array_field(profile, "library")) != 1)
        return false;
    current()->focus = 0;
    activate();
    if (!wait_for_jobs() || current()->page != SOURCES || cJSON_GetArraySize(current()->rows) != 1)
        return false;
    activate();
    if (current()->page != PLAYER)
        return false;
    video_seek(45);
    back();
    if (cJSON_GetArraySize(array_field(profile, "history")) != 1)
        return false;
    while (depth)
        back();
    current()->focus = 2;
    activate();
    type_text("test / show");
    current()->focus = cJSON_GetArraySize(current()->rows) - 1;
    activate();
    if (!wait_for_jobs() || cJSON_GetArraySize(current()->rows) != 1)
        return false;
    while (depth)
        back();
    return true;
}
static bool navigation_test(void) {
    draw();
    move_focus(1, 0);
    move_focus(-1, 0);
    activate();
    if (current()->page != CATALOGS || cJSON_GetArraySize(current()->rows) == 0)
        return false;
    draw();
    back();
    if (current()->focus != 0)
        return false;
    current()->focus = 2;
    activate();
    if (current()->page != KEYBOARD)
        return false;
    type_text("a title");
    erase_text();
    if (strcmp(current()->input, "a titl"))
        return false;
    draw();
    back();
    if (current()->focus != 2)
        return false;
    current()->focus = 6;
    activate();
    current()->focus = 6;
    activate();
    if (current()->page != PLAYER || video_state() != VID_PLAYING)
        return false;
    activate();
    if (video_state() != VID_PAUSED)
        return false;
    current()->focus = 2;
    activate();
    if (video_time() != 10)
        return false;
    draw();
    back();
    back();
    return current()->page == HOME;
}

int main(int argc, char **argv) {
    test_mode = argc > 1 && !strcmp(argv[1], "--self-test");
    srand((unsigned)time(NULL));
    umask(0077);
#ifdef WEBOS
    setenv("APPID", "tv.notflix.native", 1);
    setenv("LS2_APPID", "tv.notflix.native", 1);
    setenv("SDL_VIDEODRIVER", "wayland", 0);
    setenv("XDG_RUNTIME_DIR", "/tmp/xdg", 0);
    SDL_SetHint(SDL_HINT_WEBOS_ACCESS_POLICY_KEYS_BACK, "true");
    SDL_SetHint(SDL_HINT_WEBOS_ACCESS_POLICY_KEYS_EXIT, "true");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengles2");
#endif
    if (!setup_paths()) {
        fprintf(stderr, "Notflix: no writable app storage or asset path.\n");
        return 1;
    }
    if (!test_mode) {
        char path[1024];
        snprintf(path, sizeof path, "%s/diagnostics.log", data_dir);
        diagnostics = fopen(path, "w");
    }
    if (!diagnostics)
        diagnostics = stderr;
    setvbuf(diagnostics, NULL, _IOLBF, 0);
    fprintf(diagnostics, "Starting Notflix 0.1.1\n");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) || TTF_Init() ||
        curl_global_init(CURL_GLOBAL_DEFAULT)) {
        fprintf(diagnostics, "Platform init: %s\n", SDL_GetError());
        return 1;
    }
    IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    Uint32 flags = test_mode ? SDL_WINDOW_HIDDEN : 0;
#ifdef WEBOS
    flags |= SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_OPENGL;
#endif
    window = SDL_CreateWindow("Notflix", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIDTH,
                              HEIGHT, flags);
    if (window)
        renderer =
            SDL_CreateRenderer(window, -1,
                               test_mode ? SDL_RENDERER_SOFTWARE
                                         : SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        fprintf(diagnostics, "Renderer creation: %s (window %s)\n", SDL_GetError(),
                window ? "ready" : "failed");
        for (int i = 0; i < SDL_GetNumRenderDrivers(); i++) {
            SDL_RendererInfo info;
            if (!SDL_GetRenderDriverInfo(i, &info))
                fprintf(diagnostics, "available renderer: %s\n", info.name);
        }
        return 1;
    }
    SDL_RendererInfo info;
    if (!SDL_GetRendererInfo(renderer, &info))
        fprintf(diagnostics, "Renderer: %s\n", info.name);
    SDL_RenderSetLogicalSize(renderer, WIDTH, HEIGHT);
    int w, h;
    SDL_GetRendererOutputSize(renderer, &w, &h);
    video_set_surface(w, h);
    char ca[1024];
    snprintf(ca, sizeof ca, "%s/ca.pem", assets);
    if (!client_start(ca) || !view_open(renderer, assets)) {
        fprintf(diagnostics, "Notflix assets: %s\n", SDL_GetError());
        return 1;
    }
    fprintf(diagnostics, "Ready, drawable %d x %d\n", w, h);
    init_state();
    load_home();
    int result = 0;
    if (test_mode) {
        bool ok = navigation_test();
        const char *fixture = getenv("NOTFLIX_TEST_MANIFEST");
        if (ok && fixture)
            ok = fixture_test(fixture);
        result = ok ? 0 : 2;
    }
    for (int i = 1; i < argc; i++)
        if (strstr(argv[i], "notflixBenchmark"))
            bench_begin();
    bool text_active = false;
    Uint64 last = 0, start = SDL_GetPerformanceCounter(), freq = SDL_GetPerformanceFrequency();
    unsigned frames = 0, missed = 0;
    double worst = 0;
    while (running && !test_mode) {
        SDL_Event e;
        while (SDL_PollEvent(&e))
            events(&e);
        bool keyboard = current()->page == KEYBOARD;
        if (keyboard != text_active) {
            if (keyboard)
                SDL_StartTextInput();
            else
                SDL_StopTextInput();
            text_active = keyboard;
        }
        Request *r;
        while ((r = completed())) {
            handle_reply(r);
            request_free(r);
        }
#ifdef WEBOS
        plat_luna_pump();
#endif
        video_pump();
        player_tick();
        account_tick();
        companion_tick();
        bench_tick();
        draw();
        bench_frame();
        Uint64 now = SDL_GetPerformanceCounter();
        if (last) {
            double ms = (now - last) * 1000.0 / freq;
            unsigned slots = (unsigned)(ms / (1000.0 / 60.0) + 0.5);
            if (slots > 1)
                missed += slots - 1;
            if (ms > worst)
                worst = ms;
        }
        frames++;
        last = now;
        if (now - start >= 60 * freq) {
            fprintf(diagnostics,
                    "frame sample: %.2f fps, %u estimated missed refreshes, longest %.2f ms\n",
                    frames * (double)freq / (now - start), missed, worst);
            start = now;
            frames = missed = 0;
            worst = 0;
        }
        if (app_behind())
            SDL_Delay(50);
    }
    bench_cancel();
    companion_stop();
    while (depth)
        back();
    free_screen(current());
    client_stop();
    view_close();
    cJSON_Delete(strings);
    cJSON_Delete(db);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();
    curl_global_cleanup();
    return result;
}
