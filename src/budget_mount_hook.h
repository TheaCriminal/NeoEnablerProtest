/* GPL-3.0-only. Firmware-specific BeginAppMount2 barrier for DATA budgets.
 * Uses only userland code and DATA. No kernel TEXT probing or modification.
 */
#pragma once
#include "step_shim_data.h"

typedef struct {
 uint32_t fw, function, call, import_, got, wrapper, padding, cave, end;
 unsigned char call_bytes[5], stub[6];
} bm_profile;
static const bm_profile bm_profiles[]={
 {0x13200000,0x6d6dc0,0x6d7c11,0x195b690,0x2067458,0x42850,0x19619ec,0x1962000,0x1964000,
  {0xe8,0x7a,0x3a,0x28,1},{0xff,0x25,0xc2,0xbd,0x70,0}},
 {0x13600000,0x6d7af0,0x6d8941,0x195c3b0,0x2067460,0x42850,0x19626fc,0x1963000,0x1964000,
  {0xe8,0x6a,0x3a,0x28,1},{0xff,0x25,0xaa,0xb0,0x70,0}},
};
typedef struct {
 uint32_t enabled,active,gate,seq,response_seq,command,phase,category;
 int32_t result;
 char title[12];
 uint32_t blocked,refused,expired,dirty;
 char magic[8];
} bm_mailbox;
_Static_assert(sizeof(bm_mailbox)==MS_MAGIC+8-MS_ENABLED,"mailbox ABI");
typedef struct {
 const bm_profile *profile;
 pid_t pid; uint64_t base,lib,cave,cycles;
 unsigned char saved[5],redirect[5];
 int attached,stopped,fatal;
} bm_hook;
static bm_hook bm;
static const bm_profile *bm_find(uint32_t fw){
 for(unsigned i=0;i<sizeof bm_profiles/sizeof bm_profiles[0];i++)if(bm_profiles[i].fw==fw)return &bm_profiles[i];
 return NULL;
}
static int bm_read(uint64_t a,void *p,size_t n){return kernel_proc_copyout(bm.pid,a,p,n);}
static int bm_store(uint64_t a,const void *p,size_t n){
 unsigned char back[1024];
 return n>sizeof back||kernel_proc_copyin(bm.pid,(void*)p,a,n)||bm_read(a,back,n)||memcmp(p,back,n)?-1:0;
}
static int bm_state(void){
 /* KERN_PROC_PID is not a reliable full kinfo_proc query on this target.
  * Use the same bounded KERN_PROC_PROC enumeration as the tested observer. */
 int mib[]={CTL_KERN,KERN_PROC,KERN_PROC_PROC,0};size_t n=0;
 if(sysctl(mib,4,NULL,&n,NULL,0)||!n||n>32*1024*1024)return -1;
 n+=16*sizeof(struct kinfo_proc);unsigned char *bytes=malloc(n);if(!bytes)return -1;
 int state=-1;
 if(!sysctl(mib,4,bytes,&n,NULL,0)){
  for(size_t off=0;off+sizeof(struct kinfo_proc)<=n;){
   struct kinfo_proc *k=(void*)(bytes+off);
   if(k->ki_structsize<(int)sizeof *k||(size_t)k->ki_structsize>n-off)break;
   if(k->ki_pid==bm.pid&&!strncmp(k->ki_comm,"SceShellCore",sizeof k->ki_comm)){
    state=(unsigned char)k->ki_stat;break;
   }
   off+=(size_t)k->ki_structsize;
  }
 }
 free(bytes);return state;
}
static int bm_stop(void){
 int state=bm_state();if(state<0||state==4||kill(bm.pid,SIGSTOP))return -1;
 for(int i=0;i<100;i++){if(bm_state()==4){bm.stopped=1;return 0;}usleep(1000);}
 if(kill(bm.pid,SIGCONT)){bm.fatal=1;bm.stopped=1;}return -1;
}
static int bm_resume(void){
 if(!bm.stopped)return 0;
 if(kill(bm.pid,SIGCONT))return -1;
 bm.stopped=0;return 0;
}
static int bm_valid(void){
 const bm_profile *p=bm.profile;unsigned char b[24];uint64_t got;
 const unsigned char pro[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec,0xe8,0x18,0,0};
 const unsigned char wrapper[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec,0x98,1,0,0};
 if(bm_read(bm.base+p->function,b,sizeof pro)||memcmp(b,pro,sizeof pro)||
    bm_read(bm.base+p->call,b,5)||memcmp(b,p->call_bytes,5)||
    bm_read(bm.base+p->import_,b,6)||memcmp(b,p->stub,6)||
    bm_read(bm.base+p->got,&got,8)||got!=bm.lib+p->wrapper||
    bm_read(got,b,sizeof wrapper)||memcmp(b,wrapper,sizeof wrapper))return -1;
 return 0;
}
/* Recognize our own disarmed retained shim; never reuse its reservation. */
static int bm_retained(const unsigned char *b){
 if(memcmp(b,step_code,MS_ORIGINAL)||memcmp(b+MS_MAGIC,step_code+MS_MAGIC,sizeof step_code-MS_MAGIC))return 0;
 uint64_t original;memcpy(&original,b+MS_ORIGINAL,8);
 if(original!=bm.lib+bm.profile->wrapper)return 0;
 uint32_t v;
 const unsigned fields[]={MS_ENABLED,MS_ACTIVE,MS_GATE,MS_DIRTY};
 for(unsigned i=0;i<4;i++){memcpy(&v,b+fields[i],4);if(v)return 0;}
 for(size_t i=sizeof step_code;i<1024;i++)if(b[i])return 0;
 return 1;
}
static int bm_reserve(uint64_t *out){
 const bm_profile *p=bm.profile;unsigned char b[1024];*out=0;
 for(uint32_t off=p->padding;off<p->cave;){
  size_t n=p->cave-off;if(n>sizeof b)n=sizeof b;
  if(bm_read(bm.base+off,b,n))return -1;
  for(size_t i=0;i<n;i++)if(b[i])return -1;
  off+=(uint32_t)n;
 }
 for(uint32_t off=p->cave;off+sizeof b<=p->end;off+=sizeof b){
  if(bm_read(bm.base+off,b,sizeof b))return -1;
  size_t n=0;while(n<sizeof b&&!b[n])n++;
  if(n==sizeof b){if(!*out)*out=bm.base+off;}
  else if(!bm_retained(b))return -1;
 }
 return *out?0:-1;
}
static int bm_permissions(void){
 return kernel_get_vmem_protection(bm.pid,(bm.base+bm.profile->call)&~0x3fffULL,0x4000)==7&&
        kernel_get_vmem_protection(bm.pid,bm.cave&~0x3fffULL,0x4000)==7?0:-1;
}
static int bm_install(uint32_t fw,uint64_t cycles){
 if(bm.attached)return 0;
 memset(&bm,0,sizeof bm);bm.profile=bm_find(fw);if(!bm.profile)return -1;
 bm.pid=find_proc("SceShellCore");if(bm.pid<0)return -2;
 dynlib_obj_t main={0},lib={0};uint32_t handle;
 if(kernel_dynlib_obj(bm.pid,0,&main)||kernel_dynlib_handle(bm.pid,"libkernel_sys.sprx",&handle)||
    kernel_dynlib_obj(bm.pid,handle,&lib)||main.mapsize<bm.profile->got+8)return -3;
 bm.base=main.mapbase;bm.lib=lib.mapbase;bm.cycles=cycles;
 if(bm_valid()||bm_reserve(&bm.cave)||bm_permissions())return -4;
 int64_t disp=(int64_t)bm.cave-(int64_t)(bm.base+bm.profile->call+5);if(disp!=(int32_t)disp)return -5;
 if(bm_stop())return -6;
 uint64_t selected=0;int rc=0;
 if(bm_valid()||bm_reserve(&selected)||selected!=bm.cave||bm_permissions())rc=-7;
 unsigned char code[1024]={0};memcpy(code,step_code,sizeof step_code);
 uint64_t original=bm.lib+bm.profile->wrapper;
 memcpy(code+MS_ORIGINAL,&original,8);memcpy(code+MS_WAIT_CYCLES,&cycles,8);
 memcpy(bm.saved,bm.profile->call_bytes,5);bm.redirect[0]=0xe8;int32_t rel=(int32_t)disp;memcpy(bm.redirect+1,&rel,4);
 if(!rc&&bm_store(bm.cave,code,sizeof code)){
  unsigned char zero[1024]={0};
  if(bm_store(bm.cave,zero,sizeof zero))bm.fatal=1;
  rc=-8;
 }
 if(!rc&&bm_store(bm.base+bm.profile->call,bm.redirect,5)){
  if(bm_store(bm.base+bm.profile->call,bm.saved,5)){bm.fatal=1;return -100;}
  rc=-9;
 }else if(!rc)bm.attached=1;
 if(bm_resume()){bm.fatal=1;return -101;}
 return rc;
}
static int bm_field(unsigned off,uint32_t value){return !bm.attached?-1:bm_store(bm.cave+off,&value,4);}
static int bm_mail(bm_mailbox *out){
 return !bm.attached||bm_read(bm.cave+MS_ENABLED,out,sizeof *out)||memcmp(out->magic,"NEOMS001",8)?-1:0;
}
static int bm_command(uint32_t command,uint32_t seq){
 return bm_field(MS_COMMAND,command)||bm_field(MS_RESPONSE_SEQ,seq)?-1:0;
}
static int bm_remove(void){
 if(!bm.attached)return bm.fatal?-1:0;
 bm_mailbox m;if(bm_mail(&m)||m.gate||m.dirty)return -1;
 if(bm_field(MS_ENABLED,0)||bm_stop())return -1;
 if(bm_mail(&m)||m.active||m.gate||m.dirty){
  if(bm_resume())bm.fatal=1;
  return -1;
 }
 unsigned char b[5];int rc=0;
 if(bm_read(bm.base+bm.profile->call,b,5))rc=-1;
 else if(!memcmp(b,bm.saved,5))bm.attached=0;
 else if(memcmp(b,bm.redirect,5))rc=-1;
 else if(bm_store(bm.base+bm.profile->call,bm.saved,5)&&bm_store(bm.base+bm.profile->call,bm.saved,5)){
  bm.fatal=1;return -100;
 }else bm.attached=0;
 if(bm_resume()){bm.fatal=1;return -101;}
 return rc;
}
