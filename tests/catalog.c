#include "catalog.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(void){
    const char *manifest="https://example.org/user/config/manifest.json";
    char *u=resource_url(manifest,"catalog","movie","trending","a/b & c");
    assert(u && !strcmp(u,"https://example.org/user/config/catalog/movie/trending/search=a%2Fb%20%26%20c.json"));free(u);
    assert(!resource_url("https://example.org/bad","catalog","movie","top",NULL));
    assert(!web_url("file:///tmp/a") && !web_url("http:///") && !web_url("http://x/\na"));
    cJSON *m=cJSON_Parse("{\"resources\":[{\"name\":\"stream\",\"types\":[\"movie\"]}]}");
    assert(has_resource(m,"stream","movie") && !has_resource(m,"stream","series"));
    assert(!searchable(m));cJSON_Delete(m);
    cJSON *s=cJSON_Parse("{\"url\":\"https://example.org/film.mp4\",\"behaviorHints\":{\"proxyHeaders\":{}}}");
    assert(!plain_stream(s));cJSON_DeleteItemFromObject(s,"behaviorHints");assert(plain_stream(s));
    put_string(s,"name","Film");put_number(s,"position",18);
    char path[]="/tmp/notflix-unit-XXXXXX";int fd=mkstemp(path);assert(fd>=0);close(fd);
    assert(save_json(path,s));cJSON *disk=read_json(path);assert(!strcmp(field(disk,"name"),"Film"));
    assert(cJSON_GetObjectItem(disk,"position")->valueint==18);unlink(path);cJSON_Delete(disk);cJSON_Delete(s);
    puts("Catalog, stream and durable storage checks passed.");
}

