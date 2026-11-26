#include "catalog.h"
#include "family.h"
#include "captions.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(void) {
    const char *manifest = "https://example.org/user/config/manifest.json";
    char *u = resource_url(manifest, "catalog", "movie", "trending", "a/b & c");
    assert(
        u &&
        !strcmp(
            u,
            "https://example.org/user/config/catalog/movie/trending/search=a%2Fb%20%26%20c.json"));
    free(u);
    assert(!resource_url("https://example.org/bad", "catalog", "movie", "top", NULL));
    u = resource_url("https://example.org/manifest.json?config=demo#client", "meta", "movie", "id",
                     NULL);
    assert(u && !strcmp(u, "https://example.org/meta/movie/id.json?config=demo"));
    free(u);
    assert(!web_url("file:///tmp/a") && !web_url("http:///") && !web_url("http://x/\na"));
    cJSON *m = cJSON_Parse("{\"resources\":[{\"name\":\"stream\",\"types\":[\"movie\"]}]}");
    assert(has_resource(m, "stream", "movie") && !has_resource(m, "stream", "series"));
    assert(!searchable(m));
    cJSON_Delete(m);
    cJSON *s = cJSON_Parse(
        "{\"url\":\"https://example.org/film.mp4\",\"behaviorHints\":{\"proxyHeaders\":{}}}");
    assert(!plain_stream(s));
    cJSON_DeleteItemFromObject(s, "behaviorHints");
    assert(plain_stream(s));
    put_string(s, "name", "Film");
    put_number(s, "position", 18);
    char path[] = "/tmp/notflix-unit-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);
    assert(save_json(path, s));
    cJSON *disk = read_json(path);
    assert(!strcmp(field(disk, "name"), "Film"));
    assert(cJSON_GetObjectItem(disk, "position")->valueint == 18);
    unlink(path);
    cJSON_Delete(disk);
    cJSON_Delete(s);
    cJSON *p = cJSON_CreateObject(), *title = cJSON_CreateObject();
    assert(allowed_title(p, title));
    put_number(p, "maxAge", 7);
    assert(!allowed_title(p, title));
    put_string(title, "certification", "PG");
    assert(allowed_title(p, title));
    put_string(title, "certification", "PG-13");
    assert(!allowed_title(p, title));
    assert(!pin_set(p, "abc1") && pin_set(p, "0462") && pin_matches(p, "0462") &&
           !pin_matches(p, "0642"));
    cJSON_Delete(p);
    cJSON_Delete(title);
    char subs[] = "WEBVTT\n\n00:01.000 --> 00:03.000\nOne\n\n2\n00:00:05,300 --> "
                  "00:00:06,000\n<i>Two</i>\nlines";
    assert(captions_load(subs));
    assert(!*caption_at(0));
    assert(!strcmp(caption_at(1), "One"));
    assert(!*caption_at(3));
    assert(!strcmp(caption_at(5.5), "Two lines"));
    captions_clear();
    puts("Catalog, stream, family restrictions and durable storage checks passed.");
}
