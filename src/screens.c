#include "app.h"
#include "platform/plat.h"
#include "family.h"
#include "companion.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>

Screen stack[12]; int depth;
cJSON *db,*profile,*strings;
static unsigned sequence;
static unsigned last_save;
static double resume_at;
static char download_path[1024], download_profile[96];
static cJSON *downloading;
static const char *default_manifest="https://v3-cinemeta.strem.io/manifest.json";
static bool fetch(Screen *s,const char *url);

Screen *current(void){return &stack[depth];}
const char *tr(const char *en){const char *s=field(strings,en);return *s?s:en;}
void reload_language(void){
    cJSON_Delete(strings);strings=NULL;const char *lang=field(db,"language");
    if(!*lang || strspn(lang,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-")!=strlen(lang))return;
    char p[1024];snprintf(p,sizeof p,"%s/i18n/%s.json",assets,lang);strings=read_json(p);
}
void save(void){if(!test_mode && !save_json(state_path,db)) snprintf(current()->note,sizeof current()->note,"Could not save changes to TV storage.");}
void init_state(void){
    db=test_mode?NULL:read_json(state_path);
    if(!cJSON_IsObject(db)){cJSON_Delete(db);db=cJSON_CreateObject();}
    cJSON *profiles=array_field(db,"profiles");
    if(!cJSON_GetArraySize(profiles)){
        cJSON *p=cJSON_CreateObject();put_string(p,"name","Guest");cJSON_AddItemToArray(profiles,p);
    }
    cJSON *selected=cJSON_GetObjectItem(db,"profileIndex"); int index=cJSON_IsNumber(selected)?selected->valueint:0;
    profile=cJSON_GetArrayItem(profiles,index<0?0:index);
    if(!profile)profile=cJSON_GetArrayItem(profiles,0);
    cJSON *addons=array_field(db,"addons");
    if(!cJSON_GetArraySize(addons)){
        cJSON *m=cJSON_CreateObject();put_string(m,"name","Cinemeta");put_string(m,"manifest",default_manifest);
        cJSON *resources=array_field(m,"resources");cJSON_AddItemToArray(resources,cJSON_CreateString("meta"));
        cJSON_AddItemToArray(resources,cJSON_CreateString("catalog"));
        cJSON *catalogs=array_field(m,"catalogs");
        for(int i=0;i<2;i++){
            cJSON *c=cJSON_CreateObject();put_string(c,"type",i?"series":"movie");put_string(c,"id","top");
            put_string(c,"name","Popular");cJSON *extra=cJSON_CreateObject();put_string(extra,"name","search");
            cJSON_AddItemToArray(array_field(c,"extra"),extra);cJSON_AddItemToArray(catalogs,c);
        }
        cJSON_AddItemToArray(addons,m);
    }
    reload_language();
}
void free_screen(Screen *s){cJSON_Delete(s->rows);cJSON_Delete(s->meta);memset(s,0,sizeof *s);}
static void initialize(Screen *s,Page page,const char *title){
    s->page=page;s->token=++sequence;s->rows=cJSON_CreateArray();snprintf(s->title,sizeof s->title,"%s",title);
}
Screen *push(Page p,const char *title){
    if(depth+1>=(int)SDL_arraysize(stack))return NULL;
    Screen *s=&stack[++depth];initialize(s,p,title);return s;
}
void back(void){
    if(!depth){running=false;return;}
    if(current()->page==PLAYER){capture_progress();video_unload();}
    free_screen(current());depth--;
}
void row(Screen *s,Action action,const char *name,const cJSON *item,const char *url){
    if(!s || cJSON_GetArraySize(s->rows)>=300)return;
    if((action==OPEN_TITLE || action==PLAY_LOCAL) && !allowed_title(profile,item))return;
    cJSON *r=cJSON_CreateObject();put_number(r,"action",action);put_string(r,"name",name && *name?name:"Untitled");
    if(item)cJSON_AddItemToObject(r,"item",cJSON_Duplicate(item,1));
    if(url)put_string(r,"url",url);
    cJSON_AddItemToArray(s->rows,r);
}
static const char *name_of(cJSON *m){
    const char *n=field(m,"name");return *n?n:field(m,"title");
}
static void saved_rows(Screen *s,const char *key,Action action){
    cJSON *v;cJSON_ArrayForEach(v,array_field(profile,key))row(s,action,name_of(v),v,field(v,"file"));
}
void load_home(void){
    Screen *s=&stack[0];free_screen(s);initialize(s,HOME,tr("Home"));
    row(s,OPEN_MOVIES,tr("Movies"),NULL,NULL);row(s,OPEN_SHOWS,tr("Series"),NULL,NULL);
    row(s,SEARCH,tr("Search"),NULL,NULL);row(s,OPEN_LIBRARY,tr("Library"),NULL,NULL);
    row(s,CONTINUE_ROW,tr("Continue Watching"),NULL,NULL);row(s,PICK,tr("What to watch today?"),NULL,NULL);
    row(s,OPEN_SETTINGS,tr("Settings"),NULL,NULL);
    snprintf(s->note,sizeof s->note,"%s",field(profile,"name"));
    if(!test_mode)fetch(s,"https://v3-cinemeta.strem.io/catalog/movie/top.json");
}

static bool fetch(Screen *s,const char *url){
    if(!url || !request(JSON_JOB,s->token,url,NULL))return false;
    s->pending++;return true;
}
static void catalogs(const char *type){
    Screen *s=push(CATALOGS,tr(!strcmp(type,"movie")?"Movies":"Series"));if(!s)return;
    cJSON *m,*c;cJSON_ArrayForEach(m,array_field(db,"addons"))
        cJSON_ArrayForEach(c,cJSON_GetObjectItem(m,"catalogs")){
            if(strcmp(field(c,"type"),type))continue;
            bool required=false; cJSON *e;cJSON_ArrayForEach(e,cJSON_GetObjectItem(c,"extra"))if(cJSON_IsTrue(cJSON_GetObjectItem(e,"isRequired")))required=true;
            if(required)continue;
            char title[256];snprintf(title,sizeof title,"%s  /  %s",field(m,"name"),name_of(c));
            char *u=resource_url(field(m,"manifest"),"catalog",type,field(c,"id"),NULL);
            if(u)row(s,OPEN_CATALOG,title,c,u);
            free(u);
        }
}
static void search_for(const char *term){
    Screen *s=push(TITLES,term);if(!s)return;s->grid=true;
    cJSON *m,*c;cJSON_ArrayForEach(m,array_field(db,"addons"))
        cJSON_ArrayForEach(c,cJSON_GetObjectItem(m,"catalogs"))if(searchable(c)){
            char *u=resource_url(field(m,"manifest"),"catalog",field(c,"type"),field(c,"id"),term);
            fetch(s,u);free(u);
        }
}
static void detail_actions(Screen *s){
    cJSON_Delete(s->rows);s->rows=cJSON_CreateArray();cJSON *m=s->meta;
    snprintf(s->note,sizeof s->note,"%s",field(m,"description"));
    if(!allowed_title(profile,m)){snprintf(s->note,sizeof s->note,"Outside the profile age limit.");return;}
    bool series=!strcmp(field(m,"type"),"series");
    if(!series)row(s,FETCH_STREAMS,tr("Play"),m,NULL);
    bool saved=find_title(array_field(profile,"library"),field(m,"type"),field(m,"id"))>=0;
    row(s,SAVE_TITLE,tr(saved?"Remove from library":"Add to library"),m,NULL);
    cJSON *ep;
    cJSON_ArrayForEach(ep,cJSON_GetObjectItem(m,"videos")){
        char label[320];int season=0,episode=0;
        cJSON *a=cJSON_GetObjectItem(ep,"season"),*b=cJSON_GetObjectItem(ep,"episode");
        if(a)season=a->valueint;if(b)episode=b->valueint;
        snprintf(label,sizeof label,"S%02d E%02d   %s",season,episode,name_of(ep));
        row(s,FETCH_STREAMS,label,ep,NULL);
    }
    if(s->focus>=cJSON_GetArraySize(s->rows))s->focus=0;
}
static void open_detail(cJSON *m){
    if(!allowed_title(profile,m))return;
    Screen *s=push(DETAIL,name_of(m));if(!s)return;
    s->meta=cJSON_Duplicate(m,1);detail_actions(s);
    cJSON *a;cJSON_ArrayForEach(a,array_field(db,"addons"))if(has_resource(a,"meta",field(m,"type"))){
        char *u=resource_url(field(a,"manifest"),"meta",field(m,"type"),field(m,"id"),NULL);
        if(u){fetch(s,u);free(u);break;}
    }
}
static void sources(cJSON *m){
    Screen *parent=current();
    if(!allowed_title(profile,parent->page==DETAIL?parent->meta:m))return;
    Screen *s=push(SOURCES,tr("Sources"));if(!s)return;
    s->meta=cJSON_Duplicate(m,1);
    const char *type=field(parent->meta,"type");if(!*type)type=field(m,"type");
    put_string(s->meta,"type",type);
    if(parent->page==DETAIL && kids_active(profile))put_string(s->meta,"certification", *field(parent->meta,"certification")?field(parent->meta,"certification"):*field(parent->meta,"ageRating")?field(parent->meta,"ageRating"):field(parent->meta,"mpaaRating"));
    if(!*field(s->meta,"name"))put_string(s->meta,"name",name_of(m));
    s->automatic=cJSON_IsTrue(cJSON_GetObjectItem(db,"autoplay"));
    cJSON *a;cJSON_ArrayForEach(a,array_field(db,"addons"))if(has_resource(a,"stream",type)){
        char *u=resource_url(field(a,"manifest"),"stream",type,field(m,"id"),NULL);fetch(s,u);free(u);
    }
    if(!s->pending)snprintf(s->note,sizeof s->note,"Add a stream provider in Settings → Addons.");
}
static void keyboard(Action purpose,const char *title){
    Screen *s=push(KEYBOARD,title);if(!s)return;s->mode=purpose;
    const char *keys="abcdefghijklmnopqrstuvwxyz0123456789:/.-_?=&%+@#~";
    for(;*keys;keys++){char c[2]={*keys,0};row(s,INPUT_KEY,c,NULL,c);}
    row(s,INPUT_KEY,tr("Space"),NULL," ");row(s,INPUT_KEY,tr("Shift"),NULL,"shift");
    row(s,INPUT_KEY,tr("Delete"),NULL,"delete");row(s,INPUT_KEY,tr("Done"),NULL,"done");
}
void type_text(const char *t){
    Screen *s=current();if(s->page!=KEYBOARD)return;
    size_t len=strlen(s->input),n=strlen(t);
    if(len+n<sizeof s->input)memcpy(s->input+len,t,n+1);
}
void erase_text(void){
    Screen *s=current();size_t n=strlen(s->input);
    if(s->page==KEYBOARD && n){do{n--;}while(n && ((unsigned char)s->input[n]&0xc0)==0x80);s->input[n]=0;}
}
static void play(const char *url,cJSON *m){
    if(!url || (!web_url(url) && strncmp(url,"file://",7)))return;
    Screen *s=push(PLAYER,m?name_of(m):tr("Player"));if(!s)return;
    s->meta=m?cJSON_Duplicate(m,1):cJSON_CreateObject();
    put_string(s->meta,"playingUrl",url);
    row(s,TOGGLE_PLAY,tr("Play / pause"),NULL,NULL);row(s,SEEK_BACK,"−10 s",NULL,NULL);row(s,SEEK_NEXT,"+10 s",NULL,NULL);
    row(s,AUDIO,tr("Audio"),NULL,NULL);row(s,SUBTITLES,tr("Subtitles"),NULL,NULL);
    if(web_url(url) && !strstr(url,".m3u") && !strstr(url,".mpd"))row(s,GET_FILE,tr("Download"),NULL,NULL);
    row(s,GO_BACK,tr("Back"),NULL,NULL);
    cJSON *history=array_field(profile,"history");
    int idx=find_title(history,field(s->meta,"type"),field(s->meta,"id"));
    cJSON *saved=cJSON_GetObjectItem(cJSON_GetArrayItem(history,idx),"seconds");
    resume_at=saved?saved->valuedouble:0;last_save=SDL_GetTicks();video_load(url);
}
void capture_progress(void){
    Screen *s=current();if(s->page!=PLAYER || !*field(s->meta,"id"))return;
    double pos=video_time(),duration=video_duration();if(pos<1)return;
    cJSON *h=array_field(profile,"history");int idx=find_title(h,field(s->meta,"type"),field(s->meta,"id"));
    if(idx>=0)cJSON_DeleteItemFromArray(h,idx);
    if(!duration || pos<duration-10){
        cJSON *m=cJSON_Duplicate(s->meta,1);cJSON_DeleteItemFromObject(m,"playingUrl");put_number(m,"seconds",pos);
        cJSON_InsertItemInArray(h,0,m);if(cJSON_GetArraySize(h)>150)cJSON_DeleteItemFromArray(h,150);
    }
    save();
}
void player_tick(void){
    Screen *s=current();if(s->page!=PLAYER)return;
    if(resume_at>0 && video_state()==VID_PLAYING && video_duration()>0){if(resume_at<video_duration()-10)video_seek(resume_at);resume_at=0;}
    if(SDL_GetTicks()-last_save>=15000){capture_progress();last_save=SDL_GetTicks();}
    if(video_state()==VID_ERROR)snprintf(s->note,sizeof s->note,"%s",video_error());
    if(video_state()==VID_ENDED)back();
}
static void settings(void){
    Screen *s=push(SETTINGS,tr("Settings"));if(!s)return;
    if(kids_active(profile)){row(s,UNLOCK,tr("Switch profile"),NULL,NULL);return;}
    row(s,OPEN_ADDONS,tr("Addons"),NULL,NULL);row(s,DIRECT_URL,tr("Open media URL"),NULL,NULL);
    row(s,OPEN_PROFILES,tr("Profiles"),NULL,NULL);row(s,OPEN_LANG,tr("Language"),NULL,NULL);
    row(s,OPEN_DOWNLOADS,tr("Downloads"),NULL,NULL);row(s,TOGGLE_AUTO,cJSON_IsTrue(cJSON_GetObjectItem(db,"autoplay"))?"Autoplay · On":"Autoplay · Off",NULL,NULL);
    row(s,TEST_CLIP,tr("Playback test"),NULL,NULL);row(s,OPEN_ACCOUNT,tr("Nuvio account"),NULL,NULL);
    row(s,OPEN_REMOTE,tr("Phone remote"),NULL,NULL);
}
static void addon_list(Screen *s){
    cJSON_Delete(s->rows);s->rows=cJSON_CreateArray();row(s,ADD_MANIFEST,tr("Add manifest URL"),NULL,NULL);
    cJSON *m;cJSON_ArrayForEach(m,array_field(db,"addons")){
        if(!strcmp(field(m,"manifest"),default_manifest))continue;
        char n[256];snprintf(n,sizeof n,"%s  ·  %s",tr("Remove"),field(m,"name"));
        row(s,REMOVE_ADDON,n,NULL,field(m,"manifest"));
    }
}
static void submit_input(Screen *s){
    if(!*s->input)return;
    char value[sizeof s->input];snprintf(value,sizeof value,"%s",s->input);
    int mode=s->mode;
    if((mode==ADD_MANIFEST || mode==DIRECT_URL) && !web_url(value)){
        snprintf(s->note,sizeof s->note,"Enter a full http:// or https:// address.");return;
    }
    if(mode==NEW_PIN && !pin_set(db,value)){snprintf(s->note,sizeof s->note,"Choose a four-digit parent PIN.");return;}
    if(mode==UNLOCK && !pin_matches(db,value)){s->input[0]=0;snprintf(s->note,sizeof s->note,"Incorrect PIN.");return;}
    if((mode==ADD_PROFILE || mode==ADD_KIDS) && strlen(value)>64){snprintf(s->note,sizeof s->note,"Use a name under 65 bytes.");return;}
    if(mode==ADD_PROFILE || mode==ADD_KIDS){cJSON *v;cJSON_ArrayForEach(v,array_field(db,"profiles"))if(!strcmp(field(v,"name"),value)){snprintf(s->note,sizeof s->note,"Choose a different profile name.");return;}}
    if(mode==ADD_MANIFEST){
        char *probe=resource_url(value,"meta","movie","id",NULL);
        if(!probe){snprintf(s->note,sizeof s->note,"Enter a URL ending in /manifest.json");return;}
        free(probe);
    }
    back();
    if(mode==SEARCH)search_for(value);
    else if(mode==DIRECT_URL){cJSON *m=cJSON_CreateObject();put_string(m,"name",tr("Direct media"));play(value,m);cJSON_Delete(m);}
    else if(mode==ADD_MANIFEST)fetch(current(),value);
    else if(mode==NEW_PIN){save();keyboard(ADD_KIDS,tr("Name the KIDS profile"));}
    else if(mode==UNLOCK){profile=cJSON_GetArrayItem(array_field(db,"profiles"),0);put_number(db,"profileIndex",0);save();while(depth)back();load_home();}
    else if(mode==ADD_PROFILE || mode==ADD_KIDS){
        cJSON *p=cJSON_CreateObject();put_string(p,"name",value);if(mode==ADD_KIDS)put_number(p,"maxAge",7);
        cJSON_AddItemToArray(array_field(db,"profiles"),p);row(current(),SET_PROFILE,value,p,NULL);save();
    }
}
static void download(Screen *s){
    if(downloading){snprintf(s->note,sizeof s->note,"A download is already in progress.");return;}
    const char *url=field(s->meta,"playingUrl");
    char temp[1024];snprintf(temp,sizeof temp,"%s/media-XXXXXX",data_dir);
    int fd=mkstemp(temp);if(fd<0){snprintf(s->note,sizeof s->note,"Storage unavailable.");return;}
    close(fd);unlink(temp);snprintf(download_path,sizeof download_path,"%s.mp4",temp);
    if(request(DOWNLOAD_JOB,0,url,download_path)){
        downloading=cJSON_Duplicate(s->meta,1);cJSON_DeleteItemFromObject(downloading,"playingUrl");
        snprintf(download_profile,sizeof download_profile,"%s",field(profile,"name"));
        put_string(downloading,"file",download_path);snprintf(s->note,sizeof s->note,"Download started.");
    }
}

void activate(void){
    Screen *s=current();cJSON *r=cJSON_GetArrayItem(s->rows,s->focus);if(!r)return;
    cJSON *m=cJSON_GetObjectItem(r,"item");const char *url=field(r,"url");
    Action a=(Action)cJSON_GetObjectItem(r,"action")->valueint;
    Screen *p;
    switch(a){
    case OPEN_MOVIES: catalogs("movie");break;
    case OPEN_SHOWS: catalogs("series");break;
    case SEARCH:case DIRECT_URL:case ADD_MANIFEST:case ADD_PROFILE: keyboard(a,field(r,"name"));break;
    case ADD_KIDS:if(*field(db,"pinHash"))keyboard(ADD_KIDS,tr("Name the KIDS profile"));else keyboard(NEW_PIN,tr("Set a four-digit parent PIN"));break;
    case UNLOCK:keyboard(UNLOCK,tr("Enter parent PIN"));break;
    case OPEN_REMOTE:{
        p=push(CONFIRM,tr("Phone remote"));if(!p)break;
        if(companion_start(p->input,sizeof p->input)){snprintf(p->note,sizeof p->note,"Scan with your phone, or open %.170s",p->input);
            row(p,STOP_REMOTE,tr("Disconnect phone remote"),NULL,NULL);}
        else snprintf(p->note,sizeof p->note,"Could not open a local network connection.");
        break;}
    case STOP_REMOTE:companion_stop();back();break;
    case OPEN_ACCOUNT:case PAIR_ACCOUNT:case SYNC_ACCOUNT:case DISCONNECT_ACCOUNT:account_action(a);break;
    case OPEN_SETTINGS:settings();break;
    case OPEN_ADDONS:p=push(ADDONS,tr("Addons"));if(p)addon_list(p);break;
    case OPEN_CATALOG:p=push(TITLES,field(r,"name"));if(p){p->grid=true;fetch(p,url);}break;
    case OPEN_TITLE:if(s->page==HOME && cJSON_GetArraySize(s->meta))m=cJSON_GetArrayItem(s->meta,(SDL_GetTicks()/14000)%cJSON_GetArraySize(s->meta));open_detail(m);break;
    case FETCH_STREAMS:sources(m);break;
    case PLAY_STREAM:play(url,s->meta);break;
    case OPEN_LIBRARY:case CONTINUE_ROW:
        p=push(LIBRARY,field(r,"name"));if(p){p->grid=true;saved_rows(p,a==OPEN_LIBRARY?"library":"history",a==OPEN_LIBRARY?OPEN_TITLE:FETCH_STREAMS);}break;
    case PICK:{
        cJSON *list=array_field(profile,"library");int n=cJSON_GetArraySize(list);
        if(n)open_detail(cJSON_GetArrayItem(list,rand()%n));
        else snprintf(s->note,sizeof s->note,"Save a title to your library to pick one.");break;}
    case SAVE_TITLE:{
        cJSON *list=array_field(profile,"library");int idx=find_title(list,field(m,"type"),field(m,"id"));
        if(idx>=0)cJSON_DeleteItemFromArray(list,idx);else cJSON_AddItemToArray(list,cJSON_Duplicate(m,1));
        if(depth && stack[depth-1].page==LIBRARY && idx>=0){
            Screen *lib=&stack[depth-1];cJSON_DeleteItemFromArray(lib->rows,lib->focus);
            if(lib->focus>0 && lib->focus>=cJSON_GetArraySize(lib->rows))lib->focus--;
        }
        save();detail_actions(s);break;}
    case REMOVE_ADDON:{
        if(s->page!=CONFIRM){p=push(CONFIRM,field(r,"name"));if(p){row(p,GO_BACK,tr("Cancel"),NULL,NULL);row(p,REMOVE_ADDON,tr("Remove"),NULL,url);}break;}
        cJSON *list=array_field(db,"addons"),*v;int i=0;cJSON_ArrayForEach(v,list){
            if(!strcmp(field(v,"manifest"),url)){cJSON_DeleteItemFromArray(list,i);break;}i++;}
        back();addon_list(current());current()->focus=0;save();break;}
    case INPUT_KEY:
        if(!strcmp(url,"done"))submit_input(s);
        else if(!strcmp(url,"delete"))erase_text();
        else if(!strcmp(url,"shift"))s->automatic=!s->automatic;
        else {char c[2]={(char)(s->automatic?toupper((unsigned char)url[0]):url[0]),0};type_text(c);}break;
    case TOGGLE_PLAY:if(video_state()==VID_PAUSED)video_play();else video_pause();break;
    case SEEK_BACK:video_seek(video_time()-10);break;
    case SEEK_NEXT:video_seek(video_time()+10);break;
    case AUDIO:case SUBTITLES:{
        VidTrack tracks[48];int n=a==AUDIO?video_audio_tracks(tracks,48):video_text_tracks(tracks,48);
        p=push(TRACKS,field(r,"name"));if(!p)break;
        if(a==SUBTITLES)row(p,SELECT_SUB,tr("Off"),NULL,"-1");
        for(int i=0;i<n;i++){char idx[24];snprintf(idx,sizeof idx,"%d",tracks[i].idx);
            row(p,a==AUDIO?SELECT_AUDIO:SELECT_SUB,*tracks[i].name?tracks[i].name:tracks[i].lang,NULL,idx);}break;}
    case SELECT_AUDIO:video_select_audio(atoi(url));back();break;
    case SELECT_SUB:video_select_text(atoi(url));back();break;
    case TEST_CLIP:{
        char clip[1024];snprintf(clip,sizeof clip,"file://%s/test.mp4",assets);play(clip,NULL);break;}
    case OPEN_LANG:{
        p=push(LANGUAGES,tr("Language"));if(!p)break;
        char path[1024];snprintf(path,sizeof path,"%s/i18n/index.json",assets);cJSON *langs=read_json(path),*v;
        cJSON_ArrayForEach(v,langs)if(cJSON_IsString(v))row(p,SET_LANG,v->valuestring,NULL,v->string);
        cJSON_Delete(langs);break;}
    case SET_LANG:put_string(db,"language",url);reload_language();save();while(depth)back();load_home();break;
    case OPEN_PROFILES:{
        p=push(PROFILES,tr("Profiles"));if(!p)break;row(p,ADD_PROFILE,tr("Add profile"),NULL,NULL);row(p,ADD_KIDS,tr("Add KIDS profile (ages 7 and under)"),NULL,NULL);
        cJSON *v;cJSON_ArrayForEach(v,array_field(db,"profiles"))row(p,SET_PROFILE,field(v,"name"),v,NULL);break;}
    case SET_PROFILE:{
        companion_stop();int i=s->focus-2;profile=cJSON_GetArrayItem(array_field(db,"profiles"),i);put_number(db,"profileIndex",i);
        save();while(depth)back();load_home();break;}
    case TOGGLE_AUTO:{
        bool on=cJSON_IsTrue(cJSON_GetObjectItem(db,"autoplay"));
        cJSON_DeleteItemFromObject(db,"autoplay");cJSON_AddBoolToObject(db,"autoplay",!on);put_string(r,"name",on?"Autoplay · Off":"Autoplay · On");save();break;}
    case OPEN_DOWNLOADS:p=push(DOWNLOADS,tr("Downloads"));if(p)saved_rows(p,"downloads",PLAY_LOCAL);break;
    case GET_FILE:download(s);break;
    case PLAY_LOCAL:{
        char local[1200];snprintf(local,sizeof local,"file://%s",url);play(local,m);break;}
    case GO_BACK:back();break;
    default:break;
    }
}
void companion_input(const char *route,const char *value,const char *type){
    if(kids_active(profile))return;
    Screen *s=current();if(s->page==ACCOUNT || (s->page==KEYBOARD && (s->mode==UNLOCK || s->mode==NEW_PIN)))return;
    if(!strcmp(route,"remote")){
        if(!strcmp(value,"up"))move_focus(0,-1);else if(!strcmp(value,"down"))move_focus(0,1);
        else if(!strcmp(value,"left"))move_focus(-1,0);else if(!strcmp(value,"right"))move_focus(1,0);
        else if(!strcmp(value,"ok"))activate();else if(!strcmp(value,"back") && depth)back();
        else if(!strcmp(value,"home"))while(depth)back();
        else if(!strcmp(value,"playpause") && s->page==PLAYER){if(video_state()==VID_PAUSED)video_play();else video_pause();}
    }else if(!strcmp(route,"search")){while(depth)back();search_for(value);}
    else if(!strcmp(route,"cast")){while(depth)back();cJSON *m=cJSON_CreateObject();put_string(m,"type",type);put_string(m,"id",value);put_string(m,"name",value);open_detail(m);cJSON_Delete(m);}
}
void handle_reply(Request *r){
    if(r->type==ART_JOB){art_ready(r);return;}
    if(r->type==DOWNLOAD_JOB){
        if(downloading && !r->error[0]){
            cJSON *v;cJSON_ArrayForEach(v,array_field(db,"profiles"))if(!strcmp(field(v,"name"),download_profile)){
                cJSON_AddItemToArray(array_field(v,"downloads"),downloading);downloading=NULL;save();break;}
        }
        snprintf(current()->note,sizeof current()->note,"%s",r->error[0]?r->error:"Download complete.");
        cJSON_Delete(downloading);downloading=NULL;return;
    }
    Screen *s=NULL;for(int n=0;n<=depth;n++)if(stack[n].token==r->owner)s=&stack[n];
    if(!s)return;if(s->pending)s->pending--;
    if(r->operation){account_reply(s,r);return;}
    if(r->error[0]){snprintf(s->note,sizeof s->note,"%s",r->error);return;}
    cJSON *v;
    if(s->page==HOME){
        cJSON *features=cJSON_CreateArray();
        cJSON_ArrayForEach(v,cJSON_GetObjectItem(r->json,"metas")){
            if(cJSON_GetArraySize(features)>=8)break;
            if(*field(v,"id") && *field(v,"name") && allowed_title(profile,v))cJSON_AddItemToArray(features,cJSON_Duplicate(v,1));
        }
        cJSON_Delete(s->meta);s->meta=features;
        if(cJSON_GetArraySize(features))row(s,OPEN_TITLE,tr("Featured title"),cJSON_GetArrayItem(features,0),NULL);
    }else if(s->page==ADDONS){
        if(!*field(r->json,"id") || !cJSON_IsArray(cJSON_GetObjectItem(r->json,"resources"))){snprintf(s->note,sizeof s->note,"Invalid addon manifest.");return;}
        cJSON *addons=array_field(db,"addons");
        cJSON_ArrayForEach(v,addons)if(!strcmp(field(v,"manifest"),r->url)){snprintf(s->note,sizeof s->note,"Already installed.");return;}
        put_string(r->json,"manifest",r->url);cJSON_AddItemToArray(addons,r->json);r->json=NULL;addon_list(s);save();snprintf(s->note,sizeof s->note,"Addon installed.");
    } else if(s->page==TITLES){
        cJSON_ArrayForEach(v,cJSON_GetObjectItem(r->json,"metas"))if(*field(v,"id") && *field(v,"type"))row(s,OPEN_TITLE,name_of(v),v,NULL);
    } else if(s->page==DETAIL){
        v=cJSON_GetObjectItem(r->json,"meta");if(cJSON_IsObject(v) && !strcmp(field(v,"id"),field(s->meta,"id"))){
            cJSON_Delete(s->meta);s->meta=cJSON_Duplicate(v,1);detail_actions(s);}
    } else if(s->page==SOURCES){
        cJSON_ArrayForEach(v,cJSON_GetObjectItem(r->json,"streams"))if(plain_stream(v)){
            char title[384];snprintf(title,sizeof title,"%s  %s",field(v,"name"),*field(v,"title")?field(v,"title"):field(v,"description"));
            char *nl=strpbrk(title,"\r\n");if(nl)*nl=0;row(s,PLAY_STREAM,*title?title:tr("Play"),v,field(v,"url"));
        }
        if(!s->pending && s==current() && s->automatic && cJSON_GetArraySize(s->rows)){s->automatic=false;s->focus=0;activate();}
    }
}

