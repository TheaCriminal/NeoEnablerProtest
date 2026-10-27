/* GPL-3.0-only. Backward-compatible settings text, no device access. */
#pragma once
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
/* neo and memory_8gib are the keys of managers before per-game patches; force_neo and force_8gib are the
 * advanced overrides (Pro mode for every PS4 game, 8 GiB kept on). */
typedef struct{int neo,memory,gpu_enabled;unsigned gpu_mhz;int force_neo,force_8gib;int cpu_enabled;unsigned cpu_mhz;} neo_settings;
static neo_settings neo_settings_default(void){return(neo_settings){1,0,0,GPU_DEFAULT_MHZ,0,0,0,CPU_DEFAULT_MHZ};}
static int neo_settings_parse(const char*text,neo_settings*out){
 *out=neo_settings_default();int invalid_gpu=0,invalid_cpu=0;
 for(const char*p=text;*p;){
  const char*e=strchr(p,'\n');if(!e)e=p+strlen(p);
  size_t n=(size_t)(e-p);while(n&&(p[n-1]=='\r'||p[n-1]==' '))n--;
  char line[96];
  if(n>=sizeof line){if(!memcmp(p,"gpu_clock_",10))invalid_gpu=1;if(!memcmp(p,"cpu_clock_",10))invalid_cpu=1;}
  else{
   memcpy(line,p,n);line[n]=0;
   if(!strcmp(line,"neo=0"))out->neo=0;
   if(!strcmp(line,"neo=1"))out->neo=1;
   if(!strcmp(line,"memory_8gib=0"))out->memory=0;
   if(!strcmp(line,"memory_8gib=1"))out->memory=1;
   if(!strcmp(line,"force_neo=0"))out->force_neo=0;
   if(!strcmp(line,"force_neo=1"))out->force_neo=1;
   if(!strcmp(line,"force_8gib=0"))out->force_8gib=0;
   if(!strcmp(line,"force_8gib=1"))out->force_8gib=1;
   if(!strncmp(line,"gpu_clock_enabled=",18)){
    if(!strcmp(line+18,"0"))out->gpu_enabled=0;
    else if(!strcmp(line+18,"1"))out->gpu_enabled=1;
    else invalid_gpu=1;
   }
   if(!strncmp(line,"gpu_clock_mhz=",14)){
    char form[96];unsigned mhz=0;snprintf(form,sizeof form,"mhz=%s",line+14);
    if(gpu_form_mhz(form,&mhz)==1)out->gpu_mhz=mhz;else invalid_gpu=1;
   }
   if(!strncmp(line,"cpu_clock_enabled=",18)){
    if(!strcmp(line+18,"0"))out->cpu_enabled=0;
    else if(!strcmp(line+18,"1"))out->cpu_enabled=1;
    else invalid_cpu=1;
   }
   if(!strncmp(line,"cpu_clock_mhz=",14)){
    char form[96];unsigned mhz=0;snprintf(form,sizeof form,"mhz=%s",line+14);
    if(cpu_form_mhz(form,&mhz)==1)out->cpu_mhz=mhz;else invalid_cpu=1;
   }
  }
  p=*e?e+1:e;
 }
 if(invalid_gpu)out->gpu_enabled=0;
 if(invalid_cpu)out->cpu_enabled=0;
 return(invalid_gpu||invalid_cpu)?-1:0;
}
static int neo_settings_format(const neo_settings*s,char*out,size_t cap){
 int n=snprintf(out,cap,"neo=%d\nmemory_8gib=%d\ngpu_clock_enabled=%d\ngpu_clock_mhz=%u\nforce_neo=%d\nforce_8gib=%d\ncpu_clock_enabled=%d\ncpu_clock_mhz=%u\n",
  !!s->neo,!!s->memory,!!s->gpu_enabled,s->gpu_mhz,!!s->force_neo,!!s->force_8gib,!!s->cpu_enabled,s->cpu_mhz);
 return n>=0&&(size_t)n<cap?n:-1;
}
