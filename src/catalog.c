#include "catalog.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

const char *field(const cJSON *obj, const char *key) {
    const char *s = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(obj, key));
    return s ? s : "";
}

bool web_url(const char *url) {
    if (!url || strlen(url) > 8192 || strpbrk(url, "\r\n\t \\")) return false;
    size_t prefix = strncmp(url, "https://", 8) == 0 ? 8 :
                    strncmp(url, "http://", 7) == 0 ? 7 : 0;
    return prefix && strcspn(url + prefix, "/?#") > 0;
}

char *resource_url(const char *manifest, const char *resource, const char *type,
                   const char *id, const char *search) {
    if (!web_url(manifest) || !resource || !type || !id || !*type || !*id) return NULL;
    const char *tail = strpbrk(manifest, "?#");
    size_t len = tail ? (size_t)(tail - manifest) : strlen(manifest);
    const char suffix[] = "/manifest.json";
    if (len < sizeof(suffix)-1 || strncmp(manifest + len - sizeof(suffix)+1, suffix, sizeof(suffix)-1)) return NULL;
    len -= sizeof(suffix)-1;
    char *t = curl_easy_escape(NULL, type, 0), *i = curl_easy_escape(NULL, id, 0);
    char *q = search ? curl_easy_escape(NULL, search, 0) : NULL, *out = NULL;
    if (t && i && (!search || q)) {
        size_t cap = len + strlen(resource) + strlen(t) + strlen(i) + (q ? strlen(q) : 0) + 32;
        out = malloc(cap);
        if (out) snprintf(out, cap, "%.*s/%s/%s/%s%s%s.json", (int)len, manifest,
                          resource, t, i, q ? "/search=" : "", q ? q : "");
    }
    curl_free(t); curl_free(i); curl_free(q);
    return out;
}

static bool includes(const cJSON *arr, const char *s) {
    const cJSON *v;
    cJSON_ArrayForEach(v, arr) if (cJSON_IsString(v) && !strcmp(v->valuestring,s)) return true;
    return false;
}
bool has_resource(const cJSON *manifest, const char *resource, const char *type) {
    const cJSON *v;
    cJSON_ArrayForEach(v, cJSON_GetObjectItemCaseSensitive(manifest,"resources")) {
        const char *name = cJSON_IsString(v) ? v->valuestring : field(v,"name");
        if (strcmp(name,resource)) continue;
        cJSON *types = cJSON_GetObjectItemCaseSensitive(v,"types");
        if (!types) types = cJSON_GetObjectItemCaseSensitive(manifest,"types");
        if (!types || includes(types,type)) return true;
    }
    return false;
}

bool searchable(const cJSON *catalog) {
    const cJSON *v;
    cJSON_ArrayForEach(v,cJSON_GetObjectItemCaseSensitive(catalog,"extra"))
        if (!strcmp(field(v,"name"),"search")) return true;
    return false;
}

bool plain_stream(const cJSON *stream) {
    const cJSON *hints = cJSON_GetObjectItemCaseSensitive(stream,"behaviorHints");
    return web_url(field(stream,"url")) && !cJSON_GetObjectItemCaseSensitive(hints,"proxyHeaders") &&
           !cJSON_GetObjectItemCaseSensitive(stream,"drmKey");
}

cJSON *read_json(const char *path) {
    FILE *fp = fopen(path,"rb");
    if (!fp) return NULL;
    char *data = NULL;
    cJSON *out = NULL;
    if (!fseek(fp,0,SEEK_END)) {
        long n = ftell(fp);
        if (n >= 0 && n <= 16*1024*1024 && !fseek(fp,0,SEEK_SET) && (data=calloc((size_t)n+1,1)))
            if (fread(data,1,(size_t)n,fp)==(size_t)n) out=cJSON_Parse(data);
    }
    free(data); fclose(fp); return out;
}

bool save_json(const char *path, const cJSON *value) {
    char tmp[1024];
    if (snprintf(tmp,sizeof tmp,"%s.XXXXXX",path)>=(int)sizeof tmp) return false;
    char *json=cJSON_PrintUnformatted(value);
    if (!json) return false;
    int fd=mkstemp(tmp);
    FILE *fp=fd>=0 ? fdopen(fd,"wb") : NULL;
    bool ok=false;
    if (fp) {
        ok=fwrite(json,1,strlen(json),fp)==strlen(json) && fflush(fp)==0 && fsync(fd)==0;
        if (fclose(fp)!=0) ok=false;
        if (ok) ok=rename(tmp,path)==0;
    } else if (fd>=0) close(fd);
    if (!ok && fd>=0) unlink(tmp);
    cJSON_free(json); return ok;
}
void put_string(cJSON *o,const char *k,const char *s) {
    cJSON *v=cJSON_CreateString(s);
    if (!cJSON_ReplaceItemInObjectCaseSensitive(o,k,v)) cJSON_AddItemToObject(o,k,v);
}
void put_number(cJSON *o,const char *k,double n) {
    cJSON *v=cJSON_CreateNumber(n);
    if (!cJSON_ReplaceItemInObjectCaseSensitive(o,k,v)) cJSON_AddItemToObject(o,k,v);
}
cJSON *array_field(cJSON *obj,const char *key) {
    cJSON *a=cJSON_GetObjectItemCaseSensitive(obj,key);
    if (!cJSON_IsArray(a)) { cJSON_DeleteItemFromObjectCaseSensitive(obj,key); a=cJSON_AddArrayToObject(obj,key); }
    return a;
}
int find_title(const cJSON *list,const char *type,const char *id) {
    int idx=0; const cJSON *v;
    cJSON_ArrayForEach(v,list) {
        if (!strcmp(field(v,"id"),id) && !strcmp(field(v,"type"),type)) return idx;
        idx++;
    }
    return -1;
}
