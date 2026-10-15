/* GPL-3.0-only. Portable metadata collection, no budget/launch patches.
 * No firmware-specific offsets, kernel TEXT reads, debugger attachment,
 * remote calls, game allocations or HV unlock attempts. SDK discovery still
 * requires a compatible jailbroken environment and SDK kernel-RW setup.
 * Each line is an independent JSON object; keep stdout complete.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <ps5/kernel.h>
#define FORMAT "neo-diagnostic-v1"
static unsigned records;
static void string(const char*s){
 putchar('"');
 for(const unsigned char*p=(const unsigned char*)s;*p;p++){
  switch(*p){
  case '"':fputs("\\\"",stdout);break;case '\\':fputs("\\\\",stdout);break;
  case '\n':fputs("\\n",stdout);break;case '\r':fputs("\\r",stdout);break;case '\t':fputs("\\t",stdout);break;
  default:if(*p<32)printf("\\u%04x",*p);else putchar(*p);
  }
 }
 putchar('"');
}
static void begin(const char*type){records++;printf("{\"format\":\"" FORMAT "\",\"record\":");string(type);}
static void query(pid_t pid,const char*node){
 int mib[24];size_t count=23,size=8;uint64_t value=0;errno=0;
 int rc=sysctlnametomib(node,mib,&count);
 if(!rc){
  if(count>23){rc=-1;errno=EOVERFLOW;}
  else{mib[count++]=pid;rc=sysctl(mib,(unsigned)count,&value,&size,NULL,0);}
 }
 int error=errno;
 begin("query");printf(",\"pid\":%d,\"node\":",pid);string(node);
 printf(",\"rc\":%d,\"errno\":%d,\"returned_bytes\":%zu,\"value_hex\":",rc,error,size);
 if(!rc&&size==8)printf("\"0x%016lx\"",value);else fputs("null",stdout);
 fputs("}\n",stdout);
}
struct app_info {uint32_t id;uint64_t unknown;char title[10];uint32_t type;char rest[0x36];};
int sceKernelGetAppInfo(pid_t,struct app_info*);
static void process(pid_t pid,int game){
 struct app_info app={0};errno=0;int app_rc=game?sceKernelGetAppInfo(pid,&app):-1;int app_error=errno;
 char title[11]={0};if(!app_rc)memcpy(title,app.title,10);
 uint32_t h=0;int neo=-1,base=-1,compat=-1;
 if(game){
  neo=kernel_dynlib_handle(pid,"libSceGnmDriverForNeoMode.sprx",&h);
  base=kernel_dynlib_handle(pid,"libSceGnmDriver.sprx",&h);
  compat=kernel_dynlib_handle(pid,"libSceGnmDriverCompat1.sprx",&h);
 }
 begin("process");printf(",\"pid\":%d,\"kind\":\"%s\",\"title_id\":",pid,game?"game":"native_payload");string(title);
 printf(",\"app_info_rc\":%d,\"app_info_errno\":%d,\"neo_driver_rc\":%d,\"base_driver_rc\":%d,\"compat_driver_rc\":%d,\"mode\":",app_rc,app_error,neo,base,compat);
 string(!game?"native":!neo?"neo":!base?"base":!compat?"compat_unknown":"unknown");fputs("}\n",stdout);
 query(pid,"kern.app.dmem_size_pid");query(pid,"kern.app.fmem_size_pid");query(pid,"kern.app.fmem_avail_pid");
}
int main(void){
 setvbuf(stdout,NULL,_IONBF,0);
 uint32_t fw=0;size_t fw_size=4;errno=0;
 int fw_rc=sysctlbyname("machdep.upd_version",&fw,&fw_size,NULL,0),fw_error=errno;
 begin("session");printf(",\"firmware_raw\":\"0x%08x\",\"firmware_rc\":%d,\"firmware_errno\":%d,\"firmware_bytes\":%zu,\"sdk_raw\":\"0x%08x\",\"kernel_data_base\":\"0x%016lx\",\"scope\":\"metadata_only\",\"patches\":false,\"snapshot_atomic\":false}\n",fw,fw_rc,fw_error,fw_size,kernel_get_fw_version(),(uint64_t)KERNEL_ADDRESS_DATA_BASE);
 process(getpid(),0);
 int mib[]={CTL_KERN,KERN_PROC,KERN_PROC_PROC,0};size_t size=0;unsigned games=0;int complete=0,scan_rc=-1,scan_error=0;
 errno=0;scan_rc=sysctl(mib,4,NULL,&size,NULL,0);scan_error=errno;
 if(!scan_rc&&size&&size<=32*1024*1024-0x10000){
  size+=0x10000;unsigned char*b=malloc(size);
  if(b){
   errno=0;scan_rc=sysctl(mib,4,b,&size,NULL,0);scan_error=errno;
   if(!scan_rc){
    size_t off=0;
    while(off+sizeof(struct kinfo_proc)<=size){
     struct kinfo_proc*k=(void*)(b+off);
     if(k->ki_structsize<(int)sizeof(*k)||(size_t)k->ki_structsize>size-off)break;
     if(!strcmp(k->ki_comm,"eboot.bin")){if(games>=16)break;process(k->ki_pid,1);games++;}
     off+=(size_t)k->ki_structsize;
    }
    complete=off==size;
   }
   free(b);
  }else{scan_rc=-1;scan_error=ENOMEM;}
 }else if(!scan_rc){scan_rc=-1;scan_error=EOVERFLOW;}
 printf("{\"format\":\"" FORMAT "\",\"record\":\"end\",\"records_before_end\":%u,\"scan_complete\":%s,\"game_count\":%u,\"scan_rc\":%d,\"scan_errno\":%d}\n",records,complete?"true":"false",games,scan_rc,scan_error);
 return complete?0:1;
}
