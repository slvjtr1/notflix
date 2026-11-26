/* On-device acceptance run. Timings span successive SDL presentations and
   include input handling, networking callbacks and texture uploads. */
#include "app.h"
#include "bench.h"
#include "platform/plat.h"
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool active, entered;
static int step;
static unsigned frames, missed, video_frames, artwork_items;
static Uint64 start, last, freq;
static double worst, video_seconds;
static Uint32 next_key, checkpoint;
static FILE *report;
static const char *names[] = {"cold catalog", "warm catalog", "held arrows", "screen transitions",
                              "player overlay"};
bool bench_active(void) { return active; }
static void trace(const char *status) {
    if (report) {
        fprintf(report, "%s\n", status);
        fflush(report);
    }
}
static void close_pages(void) {
    while (depth)
        back();
}
void bench_cancel(void) {
    if (!active)
        return;
    trace("stopped");
    if (report)
        fclose(report);
    report = NULL;
    active = false;
    close_pages();
}
void bench_begin(void) {
    if (active)
        return;
    close_pages();
    const char *path = "/media/developer/apps/usr/palm/data/tv.notflix.benchmark.tsv";
    int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY | O_NOFOLLOW, 0644);
    if (fd < 0) {
        snprintf(current()->note, sizeof current()->note, "Could not open benchmark report.");
        return;
    }
    fchmod(fd, 0644);
    report = fdopen(fd, "w");
    if (!report) {
        close(fd);
        return;
    }
    active = true;
    entered = false;
    step = 0;
    freq = SDL_GetPerformanceFrequency();
    trace("scenario\trun\tfps\testimated_missed\tworst_ms\tcatalog_items\tvideo_playing_"
          "frames\tvideo_position_seconds");
}
static void catalog_page(void) {
    Screen *s = push(TITLES, "TV benchmark");
    if (!s)
        return;
    s->grid = true;
    cJSON *item;
    cJSON_ArrayForEach(item, stack[0].meta) row(s, OPEN_TITLE, field(item, "name"), item, NULL);
    if (request(JSON_JOB, s->token, "https://v3-cinemeta.strem.io/catalog/movie/top.json", NULL))
        s->pending++;
}
static int scenario(void) { return (step / 3 + 4) % 5; }
static void enter(void) {
    close_pages();
    int scene = scenario();
    if (scene == 0)
        view_reset_posters();
    if (scene <= 2)
        catalog_page();
    if (scene == 4) {
        Screen *s = current();
        s->focus = 6;
        activate();
        if (current()->page == SETTINGS) {
            current()->focus = 6;
            activate();
        }
    }
    fprintf(report, "starting %s %d\n", names[scene], step % 3 + 1);
    fflush(report);
    frames = missed = video_frames = artwork_items = 0;
    worst = video_seconds = 0;
    start = SDL_GetPerformanceCounter();
    last = 0;
    next_key = SDL_GetTicks() + 120;
    checkpoint = SDL_GetTicks() + 10000;
    entered = true;
}
void bench_tick(void) {
    if (!active)
        return;
    if (!entered)
        enter();
    Screen *s = current();
    int scene = scenario();
    if (s->page == TITLES) {
        unsigned n = cJSON_GetArraySize(s->rows);
        if (n > artwork_items)
            artwork_items = n;
    }
    if (s->page == PLAYER && video_state() == VID_PLAYING) {
        video_frames++;
        if (video_time() > video_seconds)
            video_seconds = video_time();
    }
    Uint32 t = SDL_GetTicks();
    if (scene == 4 && SDL_TICKS_PASSED(t, checkpoint)) {
        fprintf(report, "media state %d, position %.2f, duration %.2f, playing frames %u, %s\n",
                video_state(), video_time(), video_duration(), video_frames, video_error());
        fflush(report);
        checkpoint = t + 10000;
    }
    if (!SDL_TICKS_PASSED(t, next_key))
        return;
    next_key = t + (scene == 2 ? 110 : scene == 3 ? 500 : scene == 4 ? 850 : 400);
    if (scene <= 2 && s->page == TITLES) {
        int count = cJSON_GetArraySize(s->rows);
        if (count)
            s->focus = (s->focus + 1) % count;
    } else if (scene == 3) {
        if (s->page == HOME) {
            s->focus = 6;
            activate();
        } else
            back();
    } else if (scene == 4 && s->page == PLAYER) {
        controls_wake();
        move_focus(1, 0);
        if (s->focus >= 3)
            s->focus = 0;
    }
}
void bench_frame(void) {
    if (!active || !entered)
        return;
    Uint64 now = SDL_GetPerformanceCounter();
    if (last) {
        double ms = (now - last) * 1000.0 / freq;
        int slots = (int)floor(ms * 0.06 + 0.5);
        if (slots > 1)
            missed += (unsigned)(slots - 1);
        if (ms > worst)
            worst = ms;
    }
    last = now;
    frames++;
    if (now - start < 60 * freq)
        return;
    fprintf(report, "%s\t%d\t%.2f\t%u\t%.2f\t%u\t%u\t%.2f\n", names[scenario()], step % 3 + 1,
            frames * (double)freq / (now - start), missed, worst, artwork_items, video_frames,
            video_seconds);
    fflush(report);
    entered = false;
    if (++step == 15) {
        trace("complete");
        fclose(report);
        report = NULL;
        active = false;
        close_pages();
    }
}
