#include "companion.h"
#include "catalog.h"
#include "civetweb.h"
#include <SDL.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static struct mg_context *server;
static SDL_mutex *guard;
static char code[7],address[96];
typedef struct { char route[16],value[256],type[12]; } Command;
static Command queue[16];
static unsigned head,tail;
static const char page[]=
"<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'><title>Notflix remote</title>"
"<style>body{max-width:28rem;margin:3rem auto;padding:1rem;background:#111116;color:white;font:18px system-ui}h1{color:#ff3546}"
"button,input{padding:.75rem;margin:.2rem;font:inherit;color:white;background:#292930;border:1px solid #555;border-radius:.4rem}button:focus{outline:2px solid white;box-shadow:0 0 10px white}</style>"
"<h1>Notflix</h1><form><input id=q aria-label='Search titles' placeholder='Search titles'><button>Search</button></form><div id=keys></div><p role=status id=msg></p>"
"<script>const code=new URLSearchParams(location.search).get('code')||prompt('Pairing code on your TV');"
"async function send(p,data){try{let r=await fetch('/api/'+p,{method:'POST',headers:{'X-Notflix-Code':code,'Content-Type':'application/json'},body:JSON.stringify(data)});msg.textContent=r.ok?'Sent':'Check the pairing code'}catch(e){msg.textContent='TV unavailable'}}"
"document.querySelector('form').onsubmit=e=>{e.preventDefault();send('search',{query:q.value})};"
"for(const a of ['up','left','ok','right','down','back','home','playpause']){let b=document.createElement('button');b.textContent=a;b.onclick=()=>send('remote',{action:a});keys.append(b)}</script>";
static int reply(struct mg_connection *conn,int status,const char *type,const char *body){
    mg_printf(conn,"HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\nCache-Control: no-store\r\nReferrer-Policy: no-referrer\r\nX-Content-Type-Options: nosniff\r\n\r\n%s",status,status==200?"OK":"Error",type,strlen(body),body);
    return status;
}
static int handle(struct mg_connection *conn){
    const struct mg_request_info *info=mg_get_request_info(conn);const char *p=info->local_uri;
    if(!strcmp(info->request_method,"GET") && (!strcmp(p,"/") || !strcmp(p,"/remote")))return reply(conn,200,"text/html; charset=utf-8",page);
    const char *auth=mg_get_header(conn,"X-Notflix-Code");char param[16]="";
    if(!auth && info->query_string){mg_get_var(info->query_string,strlen(info->query_string),"code",param,sizeof param);auth=param;}
    if(!auth || strlen(auth)!=6 || CRYPTO_memcmp(auth,code,6))return reply(conn,403,"application/json","{\"ok\":false}");
    if(!strcmp(p,"/api/ping") && !strcmp(info->request_method,"GET"))return reply(conn,200,"application/json","{\"ok\":true,\"app\":\"notflix-tv\"}");
    if(strcmp(info->request_method,"POST") || (strcmp(p,"/api/search") && strcmp(p,"/api/remote") && strcmp(p,"/api/cast")))return reply(conn,404,"application/json","{}");
    if(info->content_length<=0 || info->content_length>2048)return reply(conn,400,"application/json","{}");
    char body[2049];int len=0;
    while(len<info->content_length){int n=mg_read(conn,body+len,(size_t)info->content_length-len);if(n<=0)return reply(conn,400,"application/json","{}");len+=n;}
    body[len]=0;cJSON *data=cJSON_Parse(body);
    const char *value=field(data,!strcmp(p,"/api/search")?"query":!strcmp(p,"/api/cast")?"id":"action");
    if(!*value && !strcmp(p,"/api/cast"))value=field(data,"itemId");
    const char *type=field(data,"type");if(!*type)type=field(data,"itemType");
    bool ok=*value && strlen(value)<sizeof queue[0].value;
    SDL_LockMutex(guard);
    if(tail-head==SDL_arraysize(queue))ok=false;
    if(ok){Command *c=&queue[tail++%SDL_arraysize(queue)];snprintf(c->route,sizeof c->route,"%s",p+5);
        snprintf(c->value,sizeof c->value,"%s",value);snprintf(c->type,sizeof c->type,"%s",!strcmp(type,"series")?"series":"movie");}
    SDL_UnlockMutex(guard);cJSON_Delete(data);
    return reply(conn,ok?200:400,"application/json",ok?"{\"ok\":true}":"{\"ok\":false}");
}
bool companion_start(char *link,int cap){
    if(!server){
        unsigned char seed[3];if(RAND_bytes(seed,3)!=1)return false;
        snprintf(code,sizeof code,"%02X%02X%02X",seed[0],seed[1],seed[2]);
        char ip[INET_ADDRSTRLEN]="";struct ifaddrs *all=NULL;
        if(getifaddrs(&all))return false;
        for(struct ifaddrs *a=all;a;a=a->ifa_next)if(a->ifa_addr && a->ifa_addr->sa_family==AF_INET && (a->ifa_flags&IFF_UP) && !(a->ifa_flags&IFF_LOOPBACK)){
            inet_ntop(AF_INET,&((struct sockaddr_in *)a->ifa_addr)->sin_addr,ip,sizeof ip);break;}
        freeifaddrs(all);if(!*ip)return false;
        char listen[64];snprintf(listen,sizeof listen,"%s:17692",ip);
        const char *opts[]={"listening_ports",listen,"num_threads","2","request_timeout_ms","2500",NULL};
        struct mg_callbacks cb={0};cb.begin_request=handle;
        guard=SDL_CreateMutex();if(!guard)return false;
        mg_init_library(0);server=mg_start(&cb,NULL,opts);
        if(!server){SDL_DestroyMutex(guard);guard=NULL;mg_exit_library();return false;}
        snprintf(address,sizeof address,"http://%s:17692/remote?code=%s",ip,code);
    }
    snprintf(link,cap,"%s",address);return true;
}
void companion_tick(void){
    if(!server)return;
    SDL_LockMutex(guard);bool ready=head!=tail;Command c;
    if(ready)c=queue[head++%SDL_arraysize(queue)];SDL_UnlockMutex(guard);
    if(ready)companion_input(c.route,c.value,c.type);
}
void companion_stop(void){
    if(!server)return;
    mg_stop(server);server=NULL;mg_exit_library();SDL_DestroyMutex(guard);guard=NULL;
    head=tail=0;memset(code,0,sizeof code);memset(address,0,sizeof address);
}
