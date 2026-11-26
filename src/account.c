#include "app.h"
#include <openssl/rand.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

enum { DISCOVER=1, START, POLL, EXCHANGE, REFRESH, LOCKS, CLOUD_LIBRARY, CLOUD_HISTORY };
static Uint32 next_poll, deadline;
static unsigned poll_screen;
static const char *key(void){return field(profile,"nuvioPublicKey");}
static const char *access_token(void){return field(profile,"nuvioAccess");}
static void message(Screen *s,const char *text){snprintf(s->note,sizeof s->note,"%s",text);}
static void menu(Screen *s){
    cJSON_Delete(s->rows);s->rows=cJSON_CreateArray();s->focus=0;s->input[0]=0;
    if(*field(profile,"nuvioRefresh")){
        row(s,SYNC_ACCOUNT,tr("Import library and progress"),NULL,NULL);
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
        const char *keys[]={"nuvioAccess","nuvioRefresh","nuvioPublicKey"};
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
        if(r->operation==EXCHANGE){message(s,"Linked. Import primary profile library and progress when ready.");break;}
        send(s,LOCKS,"/rest/v1/rpc/sync_pull_profile_locks",body,true);break;
    case LOCKS:{
        bool locked=!cJSON_IsArray(r->json);cJSON *p;
        cJSON_ArrayForEach(p,r->json){
            cJSON *index=cJSON_GetObjectItem(p,"profile_index");
            if(index && index->valueint==1 && !cJSON_IsFalse(cJSON_GetObjectItem(p,"pin_enabled")))locked=true;
        }
        if(locked){message(s,"Cloud profile requires PIN verification. Import unavailable.");break;}
        library_page(s,0);break;}
    case CLOUD_LIBRARY:
        if(!cJSON_IsArray(r->json)){message(s,"Invalid library response.");break;}
        import_rows(r->json,false);save();
        if(cJSON_GetArraySize(r->json)==100){s->mode+=100;library_page(s,s->mode);break;}
        s->mode=0;put_number(body,"p_profile_id",1);send(s,CLOUD_HISTORY,"/rest/v1/rpc/sync_pull_watch_progress",body,true);break;
    case CLOUD_HISTORY:
        if(!cJSON_IsArray(r->json)){message(s,"Invalid progress response.");break;}
        import_rows(r->json,true);save();message(s,"Primary profile library and progress imported.");break;
    }
    cJSON_Delete(body);
}

