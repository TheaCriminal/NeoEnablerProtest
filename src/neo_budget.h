/* GPL-3.0-only. NEO DMEM budget selection for verified PS5 kernel layouts.
 * Detect required access from page permissions and exact code, not exploit
 * cookies. Never disables protections or modifies page tables itself.
 */
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef NEO_BUDGET_TEST
#include <sys/sysctl.h>
#include <sys/user.h>
#include <ps5/kernel.h>
#define MB_DATA() ((uint64_t)KERNEL_ADDRESS_DATA_BASE)
#define MB_READ(a,p,n) kernel_copyout(a,p,n)
#define MB_WRITE(p,a,n) kernel_copyin(p,a,n)
static int mb_firmware(uint32_t*out){size_t n=4;return sysctlbyname("machdep.upd_version",out,&n,NULL,0)||n!=4?-1:0;}
static int mb_games(void){
 int mib[]={CTL_KERN,KERN_PROC,KERN_PROC_PROC,0};size_t n=0;
 if(sysctl(mib,4,NULL,&n,NULL,0)||!n||n>32*1024*1024)return -1;
 unsigned char*b=malloc(n);if(!b)return -1;
 if(sysctl(mib,4,b,&n,NULL,0)){free(b);return -1;}
 int count=0;size_t off=0;
 while(off+sizeof(struct kinfo_proc)<=n){
  struct kinfo_proc*k=(void*)(b+off);
  if(k->ki_structsize<(int)sizeof(*k)||(size_t)k->ki_structsize>n-off){count=-1;break;}
  if(!strcmp(k->ki_comm,"eboot.bin"))count++;
  off+=(size_t)k->ki_structsize;
 }
 if(off!=n)count=-1;free(b);return count;
}
#endif
enum {MB_UNKNOWN=-1,MB_STOCK=0,MB_8GIB=1,MB_STEP=2};
static const unsigned char mb_original[12]={0x49,0x81,0xc7,0,0,0,0x28,0xe9,0x34,2,0,0};
static const unsigned char mb_eight[12]={0x49,0xbf,0,0,0,0,2,0,0,0,0xeb,0x36};

typedef struct {
 uint32_t fw;
 uint32_t data_delta;
 uint32_t pml4i_off, pdpi_off, pmap_off;
 uint32_t baseline_off, branch_off, relay_off, cont_off;
 uint32_t areas_off;
 unsigned char continuation[10];
} mb_layout;

static const mb_layout mb_layouts[]={
 {0x13200000, 0xcb0000,
  0x3b25294, 0x3b25298, 0x3b25018,
  0x9f9044, 0x9f9076, 0x9f90b8, 0x9f92b6,
  0x34c0e50,
  {0x49,0x8b,0x7d,0x40,0xe8,0x11,0xdb,0xc0,0xff,0x41}},
 {0x13600000, 0xcc0000,
  0x3b49294, 0x3b49298, 0x3b49018,
  0xa013a4, 0xa013d6, 0xa01418, 0xa01616,
  0x3ad0e50,
  {0x49,0x8b,0x7d,0x40,0xe8,0x21,0x7f,0xc0,0xff,0x41}},
};
#define MB_LAYOUT_COUNT (sizeof mb_layouts/sizeof mb_layouts[0])

typedef struct {int state,access_ready;uint64_t text;unsigned char code[12];char detail[192];const mb_layout*layout;} mb_snapshot;
static int mb_pointer(uint64_t a){return a>=0xffff800000000000ULL&&a<0xfffffffffffff000ULL&&(a&7)==0;}

static const mb_layout*mb_find_layout(uint32_t fw){
 for(unsigned i=0;i<MB_LAYOUT_COUNT;i++)
  if(mb_layouts[i].fw==fw)return&mb_layouts[i];
 return NULL;
}

static int mb_page_access(uint64_t text,const mb_layout*L){
 uint32_t pml4i=0,pdpi=0;uint64_t table=0;
 if(MB_READ(text+L->pml4i_off,&pml4i,4)||pml4i<256||pml4i>511||
    MB_READ(text+L->pdpi_off,&pdpi,4)||pdpi>511||
    MB_READ(text+L->pmap_off,&table,8)||!mb_pointer(table)||(table&0xfff))return 0;
 uint64_t dmap=0xffff800000000000ULL|((uint64_t)pml4i<<39)|((uint64_t)pdpi<<30);
 const unsigned shifts[]={39,30,21,12};uint64_t va=text+L->branch_off;
 for(unsigned level=0;level<4;level++){
  uint64_t entry=0;
  if(MB_READ(table+((va>>shifts[level])&511)*8,&entry,8)||
     (entry&3)!=3||(entry&(1ULL<<58)))return 0;
  if(level==3)return 1;
  if(entry&0x80)return level==1||level==2;
  uint64_t pa=entry&0x000ffffffffff000ULL;
  if(!pa||pa>=0x8000000000ULL||dmap+pa<dmap||!mb_pointer(dmap+pa))return 0;
  table=dmap+pa;
 }
 return 0;
}
static int mb_check(mb_snapshot*s){
 memset(s,0,sizeof(*s));s->state=MB_UNKNOWN;uint32_t fw=0;uint64_t data=MB_DATA();
 if(mb_firmware(&fw)||data<0xffffffff00000000ULL||(data&0xfff)){
  snprintf(s->detail,sizeof s->detail,"8 GiB adjustment unavailable (firmware query failed)");return -1;
 }
 const mb_layout*L=mb_find_layout(fw);
 if(!L){snprintf(s->detail,sizeof s->detail,"8 GiB adjustment unavailable on this firmware");return -1;}
 s->layout=L;
 s->text=data-L->data_delta;
 if(!mb_page_access(s->text,L)){snprintf(s->detail,sizeof s->detail,"Kernel code access unavailable; waiting for hypervisor unlock");return -1;}
 const unsigned char baseline[]={0x49,0xbf,0,0,0,0x28,1,0,0,0};
 const unsigned char relay[]={0xe9,0xf9,1,0,0};
 unsigned char b[10];
 if(MB_READ(s->text+L->baseline_off,b,10)||memcmp(b,baseline,10)||
    MB_READ(s->text+L->relay_off,b,5)||memcmp(b,relay,5)||
    MB_READ(s->text+L->cont_off,b,10)||memcmp(b,L->continuation,10)||
    MB_READ(s->text+L->branch_off,s->code,12)){
  snprintf(s->detail,sizeof s->detail,"Kernel code unreadable or instruction layout differs");return -1;
 }
 unsigned char step[12];memcpy(step,mb_original,12);step[6]=0x38;
 if(!memcmp(s->code,mb_original,12))s->state=MB_STOCK;
 else if(!memcmp(s->code,mb_eight,12))s->state=MB_8GIB;
 else if(!memcmp(s->code,step,12))s->state=MB_STEP;
 else{snprintf(s->detail,sizeof s->detail,"Unknown memory patch; nothing written");return -1;}
 s->access_ready=1;
 snprintf(s->detail,sizeof s->detail,"%s",s->state==MB_8GIB?"8 GiB enabled for PS4 games":s->state==MB_STEP?"8 GiB disabled; previous 5.5 GiB experiment active":"8 GiB disabled; original 5.25 GiB memory restored");return 0;
}
/* Readback/rollback are bounded to this known 12-byte NEO branch. */
static int mb_set(int enable,char*msg,size_t cap){
 mb_snapshot s,again;
 if(mb_check(&s)){snprintf(msg,cap,"%s",s.detail);return 1;}
 const mb_layout*L=s.layout;
 const unsigned char*desired=enable?mb_eight:mb_original;
 if(!memcmp(s.code,desired,12)){snprintf(msg,cap,"%s",s.detail);return 0;}
 if(mb_games()!=0){snprintf(msg,cap,"Close every game before changing the 8 GiB memory");return 2;}
 for(unsigned i=1;i<=3;i++){
  signed char count=-1;
  if(MB_READ(s.text+L->areas_off+i*0x138+0x8e,&count,1)||count!=0){snprintf(msg,cap,"Application memory area %u is active or unreadable; nothing written",i);return 3;}
 }
 if(mb_check(&again)||again.text!=s.text||memcmp(again.code,s.code,12)){snprintf(msg,cap,"Memory state changed during checks; nothing written");return 4;}
 int rc=MB_WRITE(desired,s.text+L->branch_off,12);unsigned char readback[12];
 int rr=MB_READ(s.text+L->branch_off,readback,12);
 if(rc||rr||memcmp(readback,desired,12)){
  int undo=MB_WRITE(s.code,s.text+L->branch_off,12);
  int check=MB_READ(s.text+L->branch_off,readback,12);
  int restored=!undo&&!check&&!memcmp(readback,s.code,12);
  snprintf(msg,cap,"Memory write failed; rollback %s%s",restored?"verified":"FAILED",restored?"":"; do not launch games; reboot console");return restored?5:6;
 }
 snprintf(msg,cap,"%s",enable?"8 GiB enabled for PS4 games":"8 GiB disabled; original 5.25 GiB memory restored");return 0;
}
