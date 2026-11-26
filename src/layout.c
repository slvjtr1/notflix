#include "app.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static SDL_Rect visible[300];
static int count,base;
static int columns(Screen *s){return s->page==KEYBOARD?10:s->grid?6:s->page==HOME?4:s->page==PLAYER?4:1;}
void move_focus(int x,int y){
    Screen *s=current();int n=cJSON_GetArraySize(s->rows);if(!n)return;
    int cols=columns(s),next=s->focus+(y?y*cols:x);
    if(y>0 && next>=n && s->focus/cols<(n-1)/cols)next=n-1;
    if(next<0 || next>=n)return;
    if(cols>1 && x && next/cols!=s->focus/cols)return;
    s->focus=next;
}
void hit(int x,int y,bool click){
    for(int i=0;i<count;i++){SDL_Point pos={x,y};if(SDL_PointInRect(&pos,&visible[i])){
        current()->focus=base+i;if(click)activate();return;}}
}
static void wrapped(int x,int y,int width,const char *text){
    char line[115];size_t used=0;
    for(int n=0;n<2 && *text;n++){
        size_t left=strlen(text);used=left>90?90:left;
        while(used && ((unsigned char)text[used]&0xc0)==0x80)used--;
        if(left>used){size_t split=used;while(split && text[split]!=' ')split--;if(split>35)used=split;}
        memcpy(line,text,used);line[used]=0;label(x,y+n*25,width,SMALL,0xb9b9c3,line);
        text+=used;while(*text==' ')text++;
    }
}
void draw(void){
    Screen *s=current();bool film=s->page==PLAYER;
    view_begin(film);
    if(film){
        fill((SDL_Rect){0,434,WIDTH,286},0x111116,225);
        label(48,446,1100,BODY,0xffffff,s->title);
        double duration=video_duration(),now=video_time();
        fill((SDL_Rect){48,496,1184,4},0x5a5a64,255);
        if(duration>0)fill((SDL_Rect){48,496,(int)(1184*fmin(1,fmax(0,now/duration))),4},0xe50914,255);
        char tm[96];snprintf(tm,sizeof tm,"%02d:%02d   /   %02d:%02d",(int)now/60,(int)now%60,(int)duration/60,(int)duration%60);
        label(982,454,250,SMALL,0xffffff,tm);
    }else{
        label(48,24,340,BRAND,0xe50914,"NOTFLIX");
        label(48,111,1180,TITLE,0xf6f6f8,s->title);
        if(s->page==KEYBOARD){
            fill((SDL_Rect){48,192,1184,53},0x292930,255);
            size_t len=strlen(s->input),off=len>73?len-73:0;
            while(off<len && ((unsigned char)s->input[off]&0xc0)==0x80)off++;
            if(s->mode==UNLOCK || s->mode==NEW_PIN){char mask[16]={0};memset(mask,'*',SDL_min(len,sizeof mask-1));label(62,203,1150,BODY,0xffffff,mask);}
            else label(62,203,1150,BODY,0xffffff,s->input+off);
            if(s->note[0])label(55,635,1160,SMALL,0xffffff,s->note);
        }else if(s->note[0])wrapped(50,177,1170,s->note);
    }
    int cols=columns(s),n=cJSON_GetArraySize(s->rows);
    int rows=film?2:s->grid?1:s->page==KEYBOARD?6:s->page==HOME?2:6;
    int y0=film?521:s->grid?228:s->page==KEYBOARD?263:s->page==HOME?264:240;
    int gap=s->page==KEYBOARD?10:16;
    int width=(1184-(cols-1)*gap)/cols;
    int height=film?52:s->grid?300:s->page==HOME?110:s->page==KEYBOARD?50:49;
    base=SDL_max(0,(s->focus/cols-rows+1)*cols);count=SDL_min(n-base,cols*rows);
    for(int i=0;i<count;i++){
        int idx=base+i;SDL_Rect r={48+(i%cols)*(width+gap),y0+(i/cols)*(height+gap),width,height};
        visible[i]=r;cJSON *v=cJSON_GetArrayItem(s->rows,idx);
        const char *name=field(v,"name");
        bool focus=s->focus==idx;
        fill(r,focus?0x39393f:0x24242b,255);
        if(s->grid){
            cJSON *m=cJSON_GetObjectItem(v,"item");poster((SDL_Rect){r.x+1,r.y+1,r.w-2,240},field(m,"poster"));
            label(r.x+9,r.y+253,r.w-18,SMALL,0xffffff,name);
        }else {
            char key[2];if(s->page==KEYBOARD && s->automatic && strlen(name)==1 && name[0]>='a' && name[0]<='z'){
                key[0]=name[0]-32;key[1]=0;name=key;
            }
            label(r.x+15,r.y+(r.h-30)/2,r.w-27,s->page==KEYBOARD?SMALL:BODY,focus?0xffffff:0xd2d2da,name);
        }
        if(focus)focus_ring(r);
    }
    if(!film){
        const char *footer=s->pending?tr("Loading…"):!n?tr("No results"):tr("OK to select   ·   Back to return");
        label(48,669,930,SMALL,0xa5a5af,footer);
        if(n){char counter[48];snprintf(counter,sizeof counter,"%d / %d",s->focus+1,n);label(1115,669,128,SMALL,0xa5a5af,counter);}
    }else if(s->note[0]) label(48,666,1180,SMALL,0xffffff,s->note);
    view_end();
}

