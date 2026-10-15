/* Host-only tests: kernel backend is simulated, never connects to a PS5. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define NEO_BUDGET_TEST 1
static const uint64_t text=0xffffffff9f220000ULL,dmap=0xffff960000000000ULL;
static uint64_t mock_data;
static uint32_t firmware;
static unsigned char code[12];
static int games,active_area,bad_relay,writes,fail_write,fail_rollback,race,code_reads,reads,text_reads;
static int locked_level,readonly_level,missing_level,bad_root,page_read_error,large_level,fail_code_read,write_corrupt,readback_error;
static int mock_read(uint64_t,void*,size_t);
static int mock_write(const void*,uint64_t,size_t);
static int mb_firmware(uint32_t*out){*out=firmware;return 0;}
static int mb_games(void){return games;}
#define MB_DATA() mock_data
#define MB_READ(a,p,n) mock_read(a,p,n)
#define MB_WRITE(p,a,n) mock_write(p,a,n)
#include "../src/memory_control.h"
static int mock_read(uint64_t a,void*p,size_t n){
 reads++;
 /* There is deliberately no Byepervisor cookie or exploit identity. */
 assert(a!=text+0x2d29440);
 if(a==text+0x3b25294&&n==4){uint32_t v=300;memcpy(p,&v,4);return 0;}
 if(a==text+0x3b25298&&n==4){uint32_t v=0;memcpy(p,&v,4);return 0;}
 if(a==text+0x3b25018&&n==8){uint64_t v=bad_root?0:dmap+0x1000;memcpy(p,&v,8);return 0;}
 const unsigned shifts[]={39,30,21,12};uint64_t va=text+0x9f9076;
 for(unsigned l=0;l<4;l++)if(a==dmap+(l+1)*0x1000+((va>>shifts[l])&511)*8&&n==8){
  if(page_read_error)return -1;
  uint64_t e=3|((l+2)*0x1000);
  if((int)l==locked_level)e|=1ULL<<58;
  if((int)l==readonly_level)e&=~2ULL;
  if((int)l==missing_level)e&=~1ULL;
  if((int)l==large_level)e|=0x80;
  memcpy(p,&e,8);return 0;
 }
 const unsigned char baseline[]={0x49,0xbf,0,0,0,0x28,1,0,0,0};
 const unsigned char relay[]={0xe9,0xf9,1,0,0};
 const unsigned char continuation[]={0x49,0x8b,0x7d,0x40,0xe8,0x11,0xdb,0xc0,0xff,0x41};
 if(a==text+0x9f9044&&n==10){text_reads++;if(fail_code_read)return -1;memcpy(p,baseline,n);return 0;}
 if(a==text+0x9f90b8&&n==5){text_reads++;memcpy(p,relay,n);if(bad_relay)((unsigned char*)p)[0]=0;return 0;}
 if(a==text+0x9f92b6&&n==10){text_reads++;memcpy(p,continuation,n);return 0;}
 if(a==text+0x9f9076&&n==12){
  if(writes&&readback_error){readback_error=0;return -1;}
  text_reads++;if(race&&++code_reads==2)code[0]=0;
  memcpy(p,code,n);return 0;
 }
 for(unsigned i=1;i<=3;i++)if(a==text+0x34c0e50+i*0x138+0x8e&&n==1){*(signed char*)p=active_area==(int)i?1:0;return 0;}
 assert(!"unexpected read address/size");return -1;
}
static int mock_write(const void*p,uint64_t a,size_t n){
 /* No page-table writes, arbitrary offsets or capability-probe writes. */
 assert(a==text+0x9f9076&&n==12);writes++;
 if((writes==1&&fail_write)||(writes==2&&fail_rollback)){memcpy(code,p,6);return -1;}
 if(writes==1&&write_corrupt){memcpy(code,p,n);code[0]^=1;return 0;}
 memcpy(code,p,n);return 0;
}
static void reset(void){
 firmware=0x13200000;mock_data=text+0xcb0000;memcpy(code,mb_original,12);
 games=active_area=bad_relay=writes=fail_write=fail_rollback=race=code_reads=reads=text_reads=0;
 bad_root=page_read_error=fail_code_read=write_corrupt=readback_error=0;
 locked_level=readonly_level=missing_level=large_level=-1;
 memory_target=1;memory_retry_blocked=0;memory_message[0]=0;
}
int main(void){
 char msg[256];mb_snapshot s;
 reset();assert(!mb_check(&s)&&s.state==MB_STOCK&&s.access_ready);
 assert(!mb_set(1,msg,sizeof msg)&&writes==1&&!memcmp(code,mb_eight,12));
 assert(!mb_set(1,msg,sizeof msg)&&writes==1);
 games=1;assert(!mb_set(1,msg,sizeof msg)&&writes==1);games=0;
 assert(!mb_set(0,msg,sizeof msg)&&writes==2&&!memcmp(code,mb_original,12));
 reset();code[6]=0x38;assert(!mb_set(1,msg,sizeof msg)&&!memcmp(code,mb_eight,12));
 reset();games=1;assert(mb_set(1,msg,sizeof msg)==2&&writes==0);
 reset();games=-1;assert(mb_set(1,msg,sizeof msg)==2&&writes==0);
 reset();active_area=2;assert(mb_set(1,msg,sizeof msg)==3&&writes==0);
 reset();code[0]=0;assert(mb_set(1,msg,sizeof msg)==1&&writes==0);
 reset();firmware=0x13600000;assert(mb_set(1,msg,sizeof msg)&&writes==0&&reads==0);
 reset();mock_data++;assert(mb_set(1,msg,sizeof msg)&&writes==0&&reads==0);
 for(int level=0;level<4;level++){
  reset();locked_level=level;assert(mb_set(1,msg,sizeof msg)&&writes==0&&text_reads==0);
  reset();readonly_level=level;assert(mb_set(1,msg,sizeof msg)&&writes==0&&text_reads==0);
  reset();missing_level=level;assert(mb_set(1,msg,sizeof msg)&&writes==0&&text_reads==0);
 }
 reset();bad_root=1;assert(mb_set(1,msg,sizeof msg)&&writes==0&&text_reads==0);
 reset();page_read_error=1;assert(mb_set(1,msg,sizeof msg)&&writes==0&&text_reads==0);
 reset();fail_code_read=1;assert(mb_set(1,msg,sizeof msg)&&writes==0);
 for(int level=1;level<3;level++){reset();large_level=level;assert(!mb_set(1,msg,sizeof msg)&&writes==1);}
 reset();large_level=0;assert(mb_set(1,msg,sizeof msg)&&text_reads==0&&writes==0);
 reset();bad_relay=1;assert(mb_set(1,msg,sizeof msg)&&writes==0);
 reset();fail_write=1;assert(mb_set(1,msg,sizeof msg)==5&&writes==2&&!memcmp(code,mb_original,12)&&strstr(msg,"verified"));
 reset();fail_write=fail_rollback=1;assert(mb_set(1,msg,sizeof msg)==6&&writes==2&&strstr(msg,"FAILED"));
 reset();write_corrupt=1;assert(mb_set(1,msg,sizeof msg)==5&&writes==2&&!memcmp(code,mb_original,12));
 reset();readback_error=1;assert(mb_set(1,msg,sizeof msg)==5&&writes==2&&!memcmp(code,mb_original,12));
 reset();race=1;assert(mb_set(1,msg,sizeof msg)==4&&writes==0);
 reset();locked_level=3;assert(memory_poll()==1&&writes==0);locked_level=-1;
 assert(!memory_poll()&&!memcmp(code,mb_eight,12));
 reset();games=1;assert(memory_poll()==2&&writes==0);games=0;
 assert(!memory_poll()&&writes==1);
 games=1;assert(memory_request(0)==2&&!memory_target&&writes==1);games=0;
 assert(!memory_poll()&&!memcmp(code,mb_original,12)&&writes==2);
 for(int i=0;i<4;i++)assert(!memory_poll()&&writes==2);
 assert(!memory_request(1)&&writes==3);
 reset();assert(!memory_request(0)&&!memory_target&&writes==0);assert(!memory_poll()&&writes==0);
 reset();fail_write=1;assert(memory_poll()==5&&memory_retry_blocked&&writes==2);
 fail_write=0;assert(memory_poll()==5&&writes==2);assert(!memory_request(1)&&writes==3);
 reset();fail_write=fail_rollback=1;assert(memory_poll()==6&&memory_retry_blocked==2&&writes==2);
 fail_write=fail_rollback=0;assert(memory_request(1)==6&&writes==2);assert(memory_request(0)==6&&writes==2);
 puts("PASS: generic access/page guards, enable/restore/rollback, auto/deferred/manual-off/retry policy; no exploit marker required.");
 return 0;
}
