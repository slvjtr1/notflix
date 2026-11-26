/* Desktop UI tests exercise the player controls with a deterministic clock.
   The TV target links the actual webOS media adapter. */
#include "core/video.h"
#include <SDL.h>
#include <string.h>
static VidState state = VID_IDLE;
static double pos;
void video_load(const char *url) {
    state = url && *url ? VID_PLAYING : VID_ERROR;
    pos = 0;
}
void video_unload(void) { state = VID_IDLE; }
void video_play(void) { state = VID_PLAYING; }
void video_pause(void) { state = VID_PAUSED; }
void video_seek(double v) { pos = v < 0 ? 0 : v; }
double video_time(void) { return pos; }
double video_duration(void) { return 120; }
VidState video_state(void) { return state; }
const char *video_error(void) { return "Desktop test player"; }
int video_audio_tracks(VidTrack *a, int n) {
    (void)a;
    (void)n;
    return 0;
}
int video_text_tracks(VidTrack *a, int n) {
    (void)a;
    (void)n;
    return 0;
}
void video_select_audio(int i) { (void)i; }
void video_select_text(int i) { (void)i; }
void video_set_surface(int w, int h) {
    (void)w;
    (void)h;
}
void video_pump(void) {}
