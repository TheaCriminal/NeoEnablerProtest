/* GPL-3.0-only. 120 Hz video output flag of ps-patch-system: the apply_120hz_videoout attribute of the <Settings>
 * element in /data/user/patches/settings.xml. Text only, no device access. Every other attribute and line is
 * kept exactly as it is. */
#pragma once
#include <stdio.h>
#include <string.h>
#define HZ120_KEY "apply_120hz_videoout="
/* 1 on, 0 off, -1 attribute missing or not exactly "0"/"1". */
static int hz120_get(const char*xml){
 const char*p=strstr(xml,HZ120_KEY);if(!p)return -1;
 p+=sizeof HZ120_KEY-1;if(*p!='"'&&*p!='\'')return -1;
 if(p[2]!=*p)return -1;
 return p[1]=='1'?1:p[1]=='0'?0:-1;
}
/* Copies xml to out with the flag set to on. A missing attribute is added to <Settings. Returns the length
 * written (NUL terminated), or -1 when there is no <Settings> element, the attribute is malformed, or cap is too small. */
static int hz120_set(const char*xml,int on,char*out,size_t cap){
 size_t total=strlen(xml);const char*p=strstr(xml,HZ120_KEY);
 if(p){
  const char*q=p+sizeof HZ120_KEY-1;if(*q!='"'&&*q!='\'')return -1;
  const char*e=strchr(q+1,*q);if(!e)return -1;
  size_t head=(size_t)(q+1-xml),tail=strlen(e);
  if(head+1+tail+1>cap)return -1;
  memcpy(out,xml,head);out[head]=on?'1':'0';memcpy(out+head+1,e,tail+1);
  return (int)(head+1+tail);
 }
 p=strstr(xml,"<Settings");if(!p)return -1;
 const char*after=p+9;char c=*after;
 if(c!=' '&&c!='/'&&c!='>'&&c!='\t'&&c!='\r'&&c!='\n')return -1;
 char ins[48];int il=snprintf(ins,sizeof ins," " HZ120_KEY "\"%c\"",on?'1':'0');
 size_t head=(size_t)(after-xml);
 if(il<0||total+(size_t)il+1>cap)return -1;
 memcpy(out,xml,head);memcpy(out+head,ins,(size_t)il);memcpy(out+head+il,after,total-head+1);
 return (int)(total+(size_t)il);
}
