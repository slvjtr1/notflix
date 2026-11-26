#include "app.h"
#include <openssl/rand.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

enum { DISCOVER=1, START, POLL, EXCHANGE, REFRESH, LOCKS, CLOUD_LIBRARY, CLOUD_HISTORY, WRITE_ONE, SYNC_OWNER, CLOUD_ADDONS };
static Uint32 next_poll, deadline;
static unsigned poll_screen;
static const char *key(void){return field(profile,"nuvioPublicKey");}
static const char *access_token(void){return field(profile,"nuvioAccess");}
static void message(Screen *s,const char *text){snprintf(s->note,sizeof s->note,"%s",text);}
static void menu(Screen *s){
    cJSON_Delete(s->rows);s->rows=cJSON_CreateArray();s->focus=0;s->input[0]=0;
    if(*field(profile,"nuvioRefresh")){
        row(s,SYNC_ACCOUNT,tr("Sync primary profile"),NULL,NULL);
        row(s,DISCONNECT_ACCOUNT,tr("Disconnect account"),NULL,NULL);
    } else row(s,PAIR_ACCOUNT,tr("Link Nuvio account"),NULL,NULL);
}
static void send(Screen *s,int op,const char *path,const cJSON *body,bool auth){
    if(nuvio_request(s->token,op,path,*key()?key():NULL,auth?access_token():NULL,body))s->pending++;
    else message(s,"Could not start account request.");
}
void account_action(Action a){
    Screen *s=current();
    if(a==OPEN_ACCOUNT){s=push(ACCOUNT,tr("Nuvio account"));if(s){s->meta=cJSON_CreateObject();menu(s);}return;}
    if(s->pending)return;
    if(a==PAIR_ACCOUNT){send(s,DISCOVER,"/.well-known/nuvio",NULL,false);return;}
    if(a==DISCONNECT_ACCOUNT){
        const char *keys[]={"nuvioAccess","nuvioRefresh","nuvioPublicKey","syncQueue"};
        for(size_t i=0;i<SDL_arraysize(keys);i++)cJSON_DeleteItemFromObject(profile,keys[i]);
        menu(s);message(s,"Account disconnected. Imported items stay in this profile.");save();return;
    }
    if(a==SYNC_ACCOUNT){s->mode=0;
        cJSON *body=cJSON_CreateObject();put_string(body,"refresh_token",field(profile,"nuvioRefresh"));
        send(s,REFRESH,"/auth/v1/token?grant_type=refresh_token",body,false);cJSON_Delete(body);
    }
}
void account_tick(void){
    Screen *s=current();if(s->page!=ACCOUNT || s->token!=poll_screen || s->pending)return;
    if(SDL_TICKS_PASSED(SDL_GetTicks(),deadline)){poll_screen=0;menu(s);message(s,"Pairing expired.");return;}
    if(!SDL_TICKS_PASSED(SDL_GetTicks(),next_poll))return;
    cJSON *body=cJSON_CreateObject();put_string(body,"p_code",field(s->meta,"code"));
    put_string(body,"p_device_nonce",field(s->meta,"nonce"));
    send(s,POLL,"/rest/v1/rpc/poll_tv_login_session",body,false);cJSON_Delete(body);next_poll=SDL_GetTicks()+4000;
}
static void library_page(Screen *s,int offset){
    cJSON *body=cJSON_CreateObject();put_number(body,"p_profile_id",1);put_number(body,"p_limit",100);put_number(body,"p_offset",offset);
    send(s,CLOUD_LIBRARY,"/rest/v1/rpc/sync_pull_library",body,true);cJSON_Delete(body);
}
/* Local edits are journaled only after linking. The UI explicitly starts sync.
   Each mutation touches one content key and survives an interrupted request. */
static void queue_edit(const char *domain,const char *id,const char *rpc,const char *array,cJSON *entry){
    cJSON *q=array_field(profile,"syncQueue"),*old;int i=0;
    cJSON_ArrayForEach(old,q){
        if(!strcmp(field(old,"domain"),domain) && !strcmp(field(old,"id"),id)){cJSON_DeleteItemFromArray(q,i);break;}i++;}
    cJSON *job=cJSON_CreateObject();put_string(job,"domain",domain);put_string(job,"id",id);put_string(job,"rpc",rpc);
    cJSON *payload=cJSON_AddObjectToObject(job,"body");put_number(payload,"p_profile_id",1);
    cJSON_AddItemToArray(array_field(payload,array),entry);cJSON_AddItemToArray(q,job);
}
void queue_library_edit(const cJSON *m,bool remove){
    if(!*field(profile,"nuvioRefresh"))return;
    cJSON *v=cJSON_CreateObject();put_string(v,"content_id",field(m,"id"));put_string(v,"content_type",field(m,"type"));
    if(!remove){put_string(v,"name",field(m,"name"));put_string(v,"poster",field(m,"poster"));put_string(v,"poster_shape","POSTER");
        put_string(v,"description",field(m,"description"));put_number(v,"added_at",(double)time(NULL)*1000);
        cJSON *g=cJSON_GetObjectItem(m,"genres");cJSON_AddItemToObject(v,"genres",cJSON_IsArray(g)?cJSON_Duplicate(g,1):cJSON_CreateArray());}
    queue_edit("library",field(m,"id"),remove?"sync_delete_library_items":"sync_push_library_items",remove?"p_keys":"p_items",v);
}
void queue_progress(const cJSON *m,double pos,double duration){
    if(!*field(profile,"nuvioRefresh") || !*field(m,"id") || duration<=0)return;
    const char *id=*field(m,"contentId")?field(m,"contentId"):field(m,"id");
    cJSON *v=cJSON_CreateObject();put_string(v,"content_id",id);put_string(v,"content_type",field(m,"type"));put_string(v,"video_id",field(m,"id"));
    cJSON *season=cJSON_GetObjectItem(m,"season"),*episode=cJSON_GetObjectItem(m,"episode");char key[768];
    if(cJSON_IsNumber(season) && cJSON_IsNumber(episode)){put_number(v,"season",season->valueint);put_number(v,"episode",episode->valueint);
        snprintf(key,sizeof key,"%s_s%de%d",id,season->valueint,episode->valueint);}
    else snprintf(key,sizeof key,"%s",id);
    put_string(v,"progress_key",key);put_number(v,"position",pos*1000);put_number(v,"duration",duration*1000);put_number(v,"last_watched",time(NULL)*1000.0);
    queue_edit("progress",key,"sync_push_watch_progress","p_entries",v);
}
static void flush_or_pull(Screen *s){
    cJSON *job=cJSON_GetArrayItem(array_field(profile,"syncQueue"),0);
    if(!job){library_page(s,0);return;}
    char path[128];snprintf(path,sizeof path,"/rest/v1/rpc/%s",field(job,"rpc"));
    send(s,WRITE_ONE,path,cJSON_GetObjectItem(job,"body"),true);
}
static void import_rows(cJSON *records,bool history){
    cJSON *list=array_field(profile,history?"history":"library"),*v;
    cJSON_ArrayForEach(v,records){
        const char *type=field(v,"content_type"),*id=field(v,"content_id");
        if(history && *field(v,"video_id"))id=field(v,"video_id");
        if(!*type || !*id || find_title(list,type,id)>=0)continue;
        cJSON *m=cJSON_CreateObject();put_string(m,"id",id);put_string(m,"type",type);
        put_string(m,"name",*field(v,"name")?field(v,"name"):id);
        put_string(m,"poster",field(v,"poster"));put_string(m,"description",field(v,"description"));
        cJSON *pos=cJSON_GetObjectItem(v,"position");
        if(history && cJSON_IsNumber(pos))put_number(m,"seconds",pos->valuedouble/1000.0);
        cJSON_AddItemToArray(list,m);
    }
}
void account_reply(Screen *s,Request *r){
    if(s!=current() || s->page!=ACCOUNT)return;
    if(r->error[0]){poll_screen=0;menu(s);message(s,r->error);return;}
    cJSON *v=cJSON_IsArray(r->json)?cJSON_GetArrayItem(r->json,0):r->json;
    cJSON *body=cJSON_CreateObject();
    switch(r->operation){
    case DISCOVER:{
        if(strcmp(field(v,"service"),"nuvio") || strcmp(field(v,"backend_url"),"https://api.nuvio.tv") || !*field(v,"publishable_key")){message(s,"Invalid Nuvio configuration.");break;}
        put_string(profile,"nuvioPublicKey",field(v,"publishable_key"));
        unsigned char seed[24];char nonce[49];if(RAND_bytes(seed,sizeof seed)!=1){message(s,"Pairing unavailable.");break;}
        for(size_t i=0;i<sizeof seed;i++)snprintf(nonce+2*i,3,"%02x",seed[i]);
        put_string(s->meta,"nonce",nonce);put_string(body,"p_device_nonce",nonce);
        put_string(body,"p_redirect_base_url","https://nuvio.tv/link");put_string(body,"p_device_type","tv");put_string(body,"p_device_name","Notflix webOS");
        send(s,START,"/rest/v1/rpc/start_device_login_session",body,false);break;}
    case START:{
        if(!*field(v,"device_code") || !*field(v,"user_code")){message(s,"Missing pairing code.");break;}
        put_string(s->meta,"code",field(v,"device_code"));
        const char *link=field(v,"verification_uri_complete");if(!strncmp(link,"https://nuvio.tv/",17))snprintf(s->input,sizeof s->input,"%s",link);
        snprintf(s->note,sizeof s->note,"Visit nuvio.tv/link on your phone and enter %s",field(v,"user_code"));
        cJSON_Delete(s->rows);s->rows=cJSON_CreateArray();row(s,GO_BACK,tr("Cancel"),NULL,NULL);
        poll_screen=s->token;next_poll=SDL_GetTicks()+4000;deadline=SDL_GetTicks()+600000;break;}
    case POLL:
        if(!strcmp(field(v,"status"),"approved")){
            poll_screen=0;put_string(body,"code",field(s->meta,"code"));put_string(body,"device_nonce",field(s->meta,"nonce"));
            send(s,EXCHANGE,"/functions/v1/tv-logins-exchange",body,false);
        } else if(strcmp(field(v,"status"),"pending")){poll_screen=0;menu(s);message(s,"Pairing ended.");}break;
    case EXCHANGE:case REFRESH:
        if(!*field(v,"access_token") || !*field(v,"refresh_token")){message(s,"Unable to establish a session.");break;}
        put_string(profile,"nuvioAccess",field(v,"access_token"));put_string(profile,"nuvioRefresh",field(v,"refresh_token"));save();menu(s);
        if(r->operation==EXCHANGE){message(s,"Linked. Select Sync to upload local edits and import the primary profile.");break;}
        send(s,LOCKS,"/rest/v1/rpc/sync_pull_profile_locks",body,true);break;
    case LOCKS:{
        bool locked=!cJSON_IsArray(r->json);cJSON *p;
        cJSON_ArrayForEach(p,r->json){
            cJSON *index=cJSON_GetObjectItem(p,"profile_index");
            if(index && index->valueint==1 && !cJSON_IsFalse(cJSON_GetObjectItem(p,"pin_enabled")))locked=true;
        }
        if(locked){message(s,"Cloud profile requires PIN verification. Import unavailable.");break;}
        flush_or_pull(s);break;}
    case WRITE_ONE:cJSON_DeleteItemFromArray(array_field(profile,"syncQueue"),0);save();flush_or_pull(s);break;
    case CLOUD_LIBRARY:
        if(!cJSON_IsArray(r->json)){message(s,"Invalid library response.");break;}
        import_rows(r->json,false);save();
        if(cJSON_GetArraySize(r->json)==100){s->mode+=100;library_page(s,s->mode);break;}
        s->mode=0;put_number(body,"p_profile_id",1);send(s,CLOUD_HISTORY,"/rest/v1/rpc/sync_pull_watch_progress",body,true);break;
    case CLOUD_HISTORY:
        if(!cJSON_IsArray(r->json)){message(s,"Invalid progress response.");break;}
        import_rows(r->json,true);save();send(s,SYNC_OWNER,"/rest/v1/rpc/get_sync_owner",body,true);break;
    case SYNC_OWNER:{
        const char *id=cJSON_GetStringValue(r->json);if(!id || strlen(id)!=36 || strspn(id,"0123456789abcdefABCDEF-")!=36){message(s,"Library synced. Addon account unavailable.");break;}
        char endpoint[256];snprintf(endpoint,sizeof endpoint,"/rest/v1/addons?user_id=eq.%s&profile_id=eq.1&select=url,enabled&order=sort_order.asc",id);
        send(s,CLOUD_ADDONS,endpoint,NULL,true);break;}
    case CLOUD_ADDONS:{
        cJSON *a;cJSON_ArrayForEach(a,r->json){
            const char *url=field(a,"url");if(!web_url(url) || cJSON_IsFalse(cJSON_GetObjectItem(a,"enabled")))continue;
            char manifest[4096];size_t len=strlen(url);if(len>4000)continue;
            snprintf(manifest,sizeof manifest,"%s%s",url,strstr(url,"/manifest.json")?"":url[len-1]=='/'?"manifest.json":"/manifest.json");
            bool exists=false;cJSON *local;cJSON_ArrayForEach(local,array_field(db,"addons"))if(!strcmp(manifest,field(local,"manifest")))exists=true;
            if(!exists && request(JSON_JOB,s->token,manifest,NULL))s->pending++;
        }message(s,"Primary profile synced.");break;}
    }
    cJSON_Delete(body);
}

