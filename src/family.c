#include "family.h"
#include <ctype.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
bool kids_active(const cJSON *p){return cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(p,"maxAge"));}
static int rating(const char *s){
    const struct {const char *label;int age;} ratings[]={
        {"TP",0},{"ALL",0},{"A",0},{"G",0},{"U",0},{"TV-Y",0},{"TV-G",0},{"PG",7},
        {"TV-Y7",7},{"TV-Y7-FV",7},{"TV-PG",7},{"PG-13",12},{"TV-14",12},{"12A",12},
        {"R",16},{"TV-MA",16},{"NC-17",18},{"R18",18},{"X",99}};
    if(!strncasecmp(s,"ES-",3))s+=3;if(*s=='+')s++;
    for(unsigned i=0;i<sizeof ratings/sizeof *ratings;i++)if(!strcasecmp(s,ratings[i].label))return ratings[i].age;
    if(*s && strlen(s)<=2 && strspn(s,"0123456789")==strlen(s))return atoi(s);
    return -1;
}
bool allowed_title(const cJSON *person,const cJSON *m){
    if(!kids_active(person))return true;
    const char *s=field(m,"certification");if(!*s)s=field(m,"ageRating");
    if(!*s)s=field(m,"mpaaRating");
    int age=rating(s);
    return age>=0 && age<=cJSON_GetObjectItemCaseSensitive(person,"maxAge")->valueint;
}
static bool digits(const char *pin){return pin && strlen(pin)==4 && strspn(pin,"0123456789")==4;}
static void hex(const unsigned char *in,size_t n,char *out){
    const char *alphabet="0123456789abcdef";
    for(size_t i=0;i<n;i++){out[2*i]=alphabet[in[i]>>4];out[2*i+1]=alphabet[in[i]&15];}
    out[2*n]=0;
}
static bool derive(const char *pin,const char *salt,char out[65]){
    unsigned char data[32];if(!digits(pin) || strlen(salt)!=32)return false;
    if(PKCS5_PBKDF2_HMAC(pin,4,(const unsigned char *)salt,32,100000,EVP_sha256(),32,data)!=1)return false;
    hex(data,32,out);OPENSSL_cleanse(data,sizeof data);return true;
}
bool pin_set(cJSON *db,const char *pin){
    unsigned char random[16];char salt[33],digest[65];
    if(!digits(pin) || RAND_bytes(random,sizeof random)!=1)return false;
    hex(random,16,salt);if(!derive(pin,salt,digest))return false;
    put_string(db,"pinSalt",salt);put_string(db,"pinHash",digest);return true;
}
bool pin_matches(const cJSON *db,const char *pin){
    char digest[65];const char *known=field(db,"pinHash");
    return strlen(known)==64 && derive(pin,field(db,"pinSalt"),digest) && !CRYPTO_memcmp(known,digest,64);
}
