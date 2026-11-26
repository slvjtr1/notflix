#pragma once
#include "view.h"
#include "core/video.h"
typedef enum { HOME, CATALOGS, TITLES, DETAIL, SOURCES, LIBRARY, SETTINGS, ADDONS,
               KEYBOARD, PLAYER, TRACKS, LANGUAGES, PROFILES, DOWNLOADS, CONFIRM, ACCOUNT } Page;
typedef enum { OPEN_MOVIES,OPEN_SHOWS,SEARCH,OPEN_LIBRARY,OPEN_SETTINGS,OPEN_ADDONS,
               OPEN_CATALOG,OPEN_TITLE,FETCH_STREAMS,PLAY_STREAM,SAVE_TITLE,ADD_MANIFEST,
               REMOVE_ADDON,DIRECT_URL,TOGGLE_PLAY,SEEK_BACK,SEEK_NEXT,AUDIO,SUBTITLES,
               SELECT_AUDIO,SELECT_SUB,OPEN_LANG,SET_LANG,OPEN_PROFILES,SET_PROFILE,ADD_PROFILE,
               OPEN_DOWNLOADS,GET_FILE,PLAY_LOCAL,TEST_CLIP,GO_BACK,RETRY,INPUT_KEY,
               PLAY_FIRST,TOGGLE_AUTO,CONTINUE_ROW,PICK,FORGET_FILE,ADD_KIDS,NEW_PIN,UNLOCK,
               OPEN_ACCOUNT,PAIR_ACCOUNT,SYNC_ACCOUNT,DISCONNECT_ACCOUNT,OPEN_REMOTE,STOP_REMOTE,LOAD_CAPTION,TRAILER,
               HOST_PARTY,JOIN_PARTY,LEAVE_PARTY } Action;
typedef struct {
    Page page;
    unsigned token;
    char title[256],note[768],input[2048],type[64],id[512];
    int focus,pending,mode;
    bool grid,automatic;
    cJSON *rows,*meta;
} Screen;
extern cJSON *db,*profile,*strings;
extern char data_dir[896],assets[896],state_path[1024];
extern bool running,test_mode;
extern Screen stack[12];
extern int depth;
Screen *current(void);
Screen *push(Page page,const char *title);
void back(void);
void free_screen(Screen *s);
const char *tr(const char *english);
void save(void);
void row(Screen *s,Action action,const char *name,const cJSON *item,const char *url);
void load_home(void);
void activate(void);
void handle_reply(Request *r);
void draw(void);
void move_focus(int x,int y);
void hit(int x,int y,bool click);
void type_text(const char *text);
void erase_text(void);
void player_tick(void);
void controls_wake(void);
bool controls_visible(void);
void init_state(void);
void reload_language(void);
void capture_progress(void);
void party_start(Screen *s,const char *code);
void party_leave(Screen *s);
void party_reply(Screen *s,Request *r);
void party_tick(Screen *s);
void account_action(Action a);
void account_reply(Screen *s,Request *r);
void account_tick(void);

