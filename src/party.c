/* Nuvio watch-party snapshots. Members play their own source for the same
   title. Only media identifiers and playback time leave the TV. */
#include "app.h"
#include <openssl/rand.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

enum {CREATE=100, JOIN, HEARTBEAT, UPDATE, LEAVE};
static char party_id[96],member[96],device[65];
static bool host;
static unsigned owner;
static Uint32 next;
static double sequence,clock_offset;
static double now_ms(void){struct timeval ts;gettimeofday(&ts,NULL);return ts.tv_sec*1000.0+ts.tv_usec/1000.0;}
static const char *content(const cJSON *m){return *field(m,"contentId")?field(m,"contentId"):field(m,"id");}
static cJSON *playback(Screen *s){
    cJSON *o=cJSON_CreateObject();put_number(o,"seq",++sequence);
    put_number(o,"position_ms",video_time()*1000.0);put_number(o,"at",now_ms()+clock_offset);
    cJSON_AddBoolToObject(o,"paused",video_state()==VID_PAUSED);put_number(o,"rate",1);
    if(*member)put_string(o,"member_id",member);
    char key[700],season[16]="",episode[16]="";cJSON *v=cJSON_GetObjectItem(s->meta,"season");
    if(cJSON_IsNumber(v))snprintf(season,sizeof season,"%d",v->valueint);
    v=cJSON_GetObjectItem(s->meta,"episode");if(cJSON_IsNumber(v))snprintf(episode,sizeof episode,"%d",v->valueint);
    snprintf(key,sizeof key,"%s|%s|%s|%s",field(s->meta,"type"),content(s->meta),season,episode);
    put_string(o,"media_key",key);return o;
}
static void call(Screen *s,int op,const char *rpc,cJSON *body){
    char path[120];snprintf(path,sizeof path,"/rest/v1/rpc/watch_party_%s",rpc);
    put_string(body,"p_device_id",device);
    if(*party_id)put_string(body,"p_party_id",party_id);
    if(nuvio_request(s->token,op,path,field(profile,"nuvioPublicKey"),field(profile,"nuvioAccess"),body))s->pending++;
    else snprintf(s->note,sizeof s->note,"Could not send watch-party request.");
    cJSON_Delete(body);
}
void party_start(Screen *s,const char *code){
    if(s->page!=PLAYER || s->pending || *party_id)return;
    if(code && (!*code || strlen(code)>16)){snprintf(s->note,sizeof s->note,"Invalid party code.");return;}
    if(!*device){unsigned char raw[16];if(RAND_bytes(raw,16)!=1)return;
        for(int i=0;i<16;i++)snprintf(device+i*2,3,"%02x",raw[i]);}
    owner=s->token;sequence=clock_offset=0;member[0]=0;
    cJSON *o=cJSON_CreateObject();put_string(o,"p_display_name",field(profile,"name"));put_number(o,"p_profile_index",1);
    cJSON_AddNullToObject(o,"p_avatar_url");cJSON_AddNullToObject(o,"p_avatar_color");
    if(code){put_string(o,"p_code",code);call(s,JOIN,"join",o);}
    else{
        cJSON *m=cJSON_AddObjectToObject(o,"p_media");const char *type=field(s->meta,"type");
        put_string(m,"meta_type",type);put_string(m,"type",type);put_string(m,"meta_id",content(s->meta));
        put_string(m,"video_id",field(s->meta,"id"));put_string(m,"title",s->title);
        const char *keys[]={"season","episode"};for(int i=0;i<2;i++){cJSON *v=cJSON_GetObjectItem(s->meta,keys[i]);if(cJSON_IsNumber(v))put_number(m,keys[i],v->valuedouble);}
        cJSON_AddItemToObject(o,"p_playback",playback(s));call(s,CREATE,"create",o);
    }
}
void party_leave(Screen *s){
    if(s->token!=owner)return;
    if(*party_id)call(s,LEAVE,"leave",cJSON_CreateObject());
    party_id[0]=member[0]=0;owner=0;snprintf(s->note,sizeof s->note,"Left watch party.");
}
void party_tick(Screen *s){
    if(s->token!=owner || !*party_id || s->pending || !SDL_TICKS_PASSED(SDL_GetTicks(),next))return;
    cJSON *body=cJSON_CreateObject();
    if(host)cJSON_AddItemToObject(body,"p_playback",playback(s));
    call(s,host?UPDATE:HEARTBEAT,host?"update":"heartbeat",body);next=SDL_GetTicks()+4000;
}
void party_reply(Screen *s,Request *r){
    if(r->operation==LEAVE || s->token!=owner)return;
    cJSON *p=cJSON_GetObjectItem(r->json,"party"),*media=cJSON_GetObjectItem(p,"media");
    if(r->error[0] || !*field(p,"id")){
        snprintf(s->note,sizeof s->note,"%s",r->error[0]?r->error:*field(r->json,"error")?field(r->json,"error"):"Party ended.");
        party_id[0]=0;owner=0;return;
    }
    snprintf(party_id,sizeof party_id,"%s",field(p,"id"));
    if(strcmp(field(media,"meta_id"),content(s->meta)) || (*field(media,"video_id") && strcmp(field(media,"video_id"),field(s->meta,"id")))){
        party_leave(s);snprintf(s->note,sizeof s->note,"Open the same title and episode, then join.");return;
    }
    snprintf(member,sizeof member,"%s",field(r->json,"member_id"));host=*member && !strcmp(member,field(p,"host_member_id"));
    cJSON *clock=cJSON_GetObjectItem(r->json,"server_time_ms");if(cJSON_IsNumber(clock))clock_offset=clock->valuedouble-now_ms();
    cJSON *pb=cJSON_GetObjectItem(p,"playback"),*seq=cJSON_GetObjectItem(pb,"seq");
    if(!host && cJSON_IsNumber(seq) && seq->valuedouble>=sequence){
        sequence=seq->valuedouble;bool paused=cJSON_IsTrue(cJSON_GetObjectItem(pb,"paused"));
        cJSON *pos=cJSON_GetObjectItem(pb,"position_ms"),*at=cJSON_GetObjectItem(pb,"at");
        if(cJSON_IsNumber(pos)){double t=pos->valuedouble;
            if(!paused && cJSON_IsNumber(at))t+=fmax(0,now_ms()+clock_offset-at->valuedouble);
            if(fabs(video_time()-t/1000)>1.5)video_seek(t/1000);}
        if(paused && video_state()==VID_PLAYING)video_pause();else if(!paused && video_state()==VID_PAUSED)video_play();
    }
    snprintf(s->note,sizeof s->note,"Watch party %s  ·  %s",field(p,"code"),host?"Hosting":"Following host");next=SDL_GetTicks()+4000;
}
