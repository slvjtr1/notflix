#include "client.h"
#include <SDL_image.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct { Request *first,*last; } Queue;
static Queue jobs[3],results;
static SDL_mutex *lock;
static SDL_cond *wake;
static SDL_Thread *threads[3];
static SDL_atomic_t quitting;
static char cert[1024];

static void append(Queue *q,Request *r) {
    r->next=NULL;
    if(q->last) q->last->next=r; else q->first=r;
    q->last=r;
}
static Request *pop(Queue *q) {
    Request *r=q->first;
    if(r && !(q->first=r->next)) q->last=NULL;
    return r;
}
typedef struct { char *bytes; size_t size,cap,limit; } Buffer;
static size_t receive(char *p,size_t size,size_t count,void *ctx) {
    Buffer *b=ctx; size_t n=size*count;
    if(n>b->limit-b->size) return 0;
    size_t need=b->size+n+1;
    if(need>b->cap){
        size_t cap=need*2; char *s=realloc(b->bytes,cap);
        if(!s) return 0;
        b->bytes=s; b->cap=cap;
    }
    memcpy(b->bytes+b->size,p,n); b->size+=n; b->bytes[b->size]=0;
    return n;
}
static int progress(void *ctx,curl_off_t total,curl_off_t now,curl_off_t u,curl_off_t sent) {
    (void)ctx;(void)total;(void)now;(void)u;(void)sent; return SDL_AtomicGet(&quitting);
}
static void perform(Request *r) {
    CURL *h=curl_easy_init();
    Buffer b={.limit=r->type==ART_JOB?8*1024*1024:4*1024*1024};
    FILE *file=NULL; char part[1100]="";
    if(!h) { snprintf(r->error,sizeof r->error,"Network initialization failed"); return; }
    curl_easy_setopt(h,CURLOPT_URL,r->url);
    curl_easy_setopt(h,CURLOPT_PROTOCOLS,(long)(CURLPROTO_HTTP|CURLPROTO_HTTPS));
    curl_easy_setopt(h,CURLOPT_REDIR_PROTOCOLS,(long)(CURLPROTO_HTTP|CURLPROTO_HTTPS));
    curl_easy_setopt(h,CURLOPT_FOLLOWLOCATION,1L);
    curl_easy_setopt(h,CURLOPT_MAXREDIRS,5L);
    curl_easy_setopt(h,CURLOPT_FAILONERROR,1L);
    curl_easy_setopt(h,CURLOPT_ACCEPT_ENCODING,"");
    curl_easy_setopt(h,CURLOPT_USERAGENT,"Notflix-webOS/0.1");
    curl_easy_setopt(h,CURLOPT_NOSIGNAL,1L);
    curl_easy_setopt(h,CURLOPT_CONNECTTIMEOUT,8L);
    curl_easy_setopt(h,CURLOPT_TIMEOUT,r->type==DOWNLOAD_JOB?0L:18L);
    curl_easy_setopt(h,CURLOPT_LOW_SPEED_LIMIT,64L);
    curl_easy_setopt(h,CURLOPT_LOW_SPEED_TIME,20L);
    curl_easy_setopt(h,CURLOPT_XFERINFOFUNCTION,progress);
    curl_easy_setopt(h,CURLOPT_NOPROGRESS,0L);
    if(cert[0]) curl_easy_setopt(h,CURLOPT_CAINFO,cert);
    if (r->type==DOWNLOAD_JOB) {
        snprintf(part,sizeof part,"%s.part",r->path);
        file=fopen(part,"wb");
        if(!file) { snprintf(r->error,sizeof r->error,"Cannot create download"); goto done; }
        curl_easy_setopt(h,CURLOPT_WRITEDATA,file);
    } else {
        curl_easy_setopt(h,CURLOPT_WRITEFUNCTION,receive);
        curl_easy_setopt(h,CURLOPT_WRITEDATA,&b);
    }
    CURLcode code=curl_easy_perform(h);
    if (code!=CURLE_OK) snprintf(r->error,sizeof r->error,"%s",curl_easy_strerror(code));
    if(file) {
        if(fclose(file) || (!r->error[0] && rename(part,r->path))) snprintf(r->error,sizeof r->error,"Cannot save download");
        if(r->error[0]) unlink(part);
    } else if(!r->error[0] && r->type==JSON_JOB) {
        r->json=b.bytes?cJSON_Parse(b.bytes):NULL;
        if(!r->json) snprintf(r->error,sizeof r->error,"Invalid provider response");
    } else if(!r->error[0] && b.size) {
        SDL_RWops *rw=SDL_RWFromConstMem(b.bytes,(int)b.size);
        if(rw) r->image=IMG_Load_RW(rw,1);
    }
done:
    curl_easy_cleanup(h); free(b.bytes);
}
static int worker(void *ctx) {
    Queue *q=ctx;
    for(;;){
        SDL_LockMutex(lock);
        while(!SDL_AtomicGet(&quitting) && !q->first) SDL_CondWait(wake,lock);
        if(SDL_AtomicGet(&quitting)) { SDL_UnlockMutex(lock); break; }
        Request *r=pop(q); SDL_UnlockMutex(lock);
        perform(r);
        SDL_LockMutex(lock); append(&results,r); SDL_UnlockMutex(lock);
    }
    return 0;
}
bool client_start(const char *ca) {
    if(ca && !access(ca,R_OK)) snprintf(cert,sizeof cert,"%s",ca);
    lock=SDL_CreateMutex(); wake=SDL_CreateCond();
    if(!lock || !wake) return false;
    for(int i=0;i<3;i++) if(!(threads[i]=SDL_CreateThread(worker,"notflix-io",&jobs[i]))) return false;
    return true;
}
bool request(JobType type,unsigned owner,const char *url,const char *path) {
    if(!web_url(url)) return false;
    Request *r=calloc(1,sizeof *r); if(!r) return false;
    r->type=type;r->owner=owner;r->url=strdup(url);r->path=path?strdup(path):NULL;
    if(!r->url || (path&&!r->path)){request_free(r);return false;}
    SDL_LockMutex(lock); append(&jobs[type],r); SDL_CondBroadcast(wake); SDL_UnlockMutex(lock);return true;
}
Request *completed(void) { SDL_LockMutex(lock); Request *r=pop(&results); SDL_UnlockMutex(lock);return r; }
void request_free(Request *r) {
    if(!r)return;
    free(r->url);free(r->path);cJSON_Delete(r->json);if(r->image)SDL_FreeSurface(r->image);free(r);
}
void client_stop(void) {
    SDL_AtomicSet(&quitting,1);
    if(lock && wake){SDL_LockMutex(lock); SDL_CondBroadcast(wake); SDL_UnlockMutex(lock);}
    for(int i=0;i<3;i++){if(threads[i])SDL_WaitThread(threads[i],NULL);Request *r;while((r=pop(&jobs[i])))request_free(r);}
    Request *r;while((r=pop(&results)))request_free(r);
    if(wake)SDL_DestroyCond(wake);
    if(lock)SDL_DestroyMutex(lock);
}
