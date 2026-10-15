/* GPL-3.0-only. Bounded form-body reader; do not act on partial POST bodies. */
#pragma once
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#define NEO_HTTP_BODY_MAX 256U
static int neo_http_read(int fd,char*buf,size_t cap,char**body){
 size_t used=0,total=0;int headers=0;
 for(;;){
  if(used>=cap-1)return -1;
  ssize_t got=read(fd,buf+used,cap-1-used);
  if(got<=0)return -1;
  if(memchr(buf+used,0,(size_t)got))return -1;
  used+=(size_t)got;buf[used]=0;
  if(!headers){
   char*end=strstr(buf,"\r\n\r\n");if(!end)continue;
   size_t length=0;int found=0;char*p=strstr(buf,"\r\n");if(!p)return -1;
   for(p+=2;p<end;){
    char*next=strstr(p,"\r\n");if(!next||next>end)return -1;
    if(!strncasecmp(p,"Transfer-Encoding:",18))return -1;
    if(!strncasecmp(p,"Content-Length:",15)){
     if(found++)return -1;
     const char*q=p+15;while(q<next&&(*q==' '||*q=='\t'))q++;
     if(q==next||*q<'0'||*q>'9')return -1;
     while(q<next&&*q>='0'&&*q<='9'){
      length=length*10+(unsigned)(*q++-'0');if(length>NEO_HTTP_BODY_MAX)return -1;
     }
     while(q<next&&(*q==' '||*q=='\t'))q++;
     if(q!=next)return -1;
    }
    p=next+2;
   }
   total=(size_t)(end+4-buf)+length;if(total>=cap)return -1;
   *body=end+4;headers=1;
  }
  if(used>=total){buf[total]=0;return 0;}
 }
}
