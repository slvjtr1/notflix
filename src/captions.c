#include "captions.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

typedef struct {double start,end;char *text;} Cue;
static Cue *cues;
static size_t count;
void captions_clear(void){for(size_t i=0;i<count;i++)free(cues[i].text);free(cues);cues=NULL;count=0;}
static double stamp(const char *text){
    int h=0,m=0;double s=0;
    if(sscanf(text,"%d:%d:%lf",&h,&m,&s)!=3 && sscanf(text,"%d:%lf",&m,&s)!=2)return -1;
    if(h<0 || m<0 || m>59 || !isfinite(s) || s<0 || s>=60)return -1;
    return h*3600.0+m*60.0+s;
}
static int compare(const void *a,const void *b){double x=((const Cue*)a)->start,y=((const Cue*)b)->start;return (x>y)-(x<y);}
bool captions_load(char *text){
    captions_clear();if(!text)return false;
    size_t cap=0;char *cursor=text;
    while(*cursor && count<25000){
        char *line=strsep(&cursor,"\n");if(!cursor)cursor=line+strlen(line);
        char *arrow=strstr(line,"-->");if(!arrow)continue;
        *arrow=0;for(char *p=line;*p;p++)if(*p==',')*p='.';
        char *to=arrow+3;for(char *p=to;*p;p++)if(*p==',')*p='.';
        double a=stamp(line),b=stamp(to);if(a<0 || b<=a)continue;
        char body[1024]="";size_t used=0;
        while(*cursor){
            line=strsep(&cursor,"\n");if(!cursor)cursor=line+strlen(line);
            line[strcspn(line,"\r")]=0;if(!*line)break;
            if(used && used<sizeof body-2)body[used++]=' ';
            bool tag=false;for(char *p=line;*p && used<sizeof body-1;p++){
                if(*p=='<')tag=true;else if(*p=='>')tag=false;else if(!tag)body[used++]=*p;}
        }
        if(!used)continue;body[used]=0;
        if(count==cap){size_t next=cap?cap*2:64;Cue *v=realloc(cues,next*sizeof *v);if(!v)break;cues=v;cap=next;}
        char *copy=strdup(body);if(!copy)break;cues[count++]=(Cue){a,b,copy};
    }
    if(count)qsort(cues,count,sizeof *cues,compare);return count>0;
}
const char *caption_at(double t){
    size_t lo=0,hi=count;
    while(lo<hi){size_t mid=lo+(hi-lo)/2;if(cues[mid].start<=t)lo=mid+1;else hi=mid;}
    if(lo && t<cues[lo-1].end)return cues[lo-1].text;return "";
}
