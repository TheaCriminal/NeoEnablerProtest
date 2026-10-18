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
static const mb_layout*cur_layout;
static int mock_read(uint64_t a,void*p,size_t n){
 reads++;
 if(!cur_layout)return -1;
 assert(a!=text+0x2d29440);
 if(a==text+cur_layout->pml4i_off&&n==4){uint32_t v=300;memcpy(p,&v,4);return 0;}
 if(a==text+cur_layout->pdpi_off&&n==4){uint32_t v=0;memcpy(p,&v,4);return 0;}
 if(a==text+cur_layout->pmap_off&&n==8){uint64_t v=bad_root?0:dmap+0x1000;memcpy(p,&v,8);return 0;}
 const unsigned shifts[]={39,30,21,12};uint64_t va=text+cur_layout->branch_off;
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
 if(a==text+cur_layout->baseline_off&&n==10){text_reads++;if(fail_code_read)return -1;memcpy(p,baseline,n);return 0;}
 if(a==text+cur_layout->relay_off&&n==5){text_reads++;memcpy(p,relay,n);if(bad_relay)((unsigned char*)p)[0]=0;return 0;}
 if(a==text+cur_layout->cont_off&&n==10){text_reads++;memcpy(p,cur_layout->continuation,n);return 0;}
 if(a==text+cur_layout->branch_off&&n==12){
  if(writes&&readback_error){readback_error=0;return -1;}
  text_reads++;if(race&&++code_reads==2)code[0]=0;
  memcpy(p,code,n);return 0;
 }
 for(unsigned i=1;i<=3;i++)if(a==text+cur_layout->areas_off+i*0x138+0x8e&&n==1){*(signed char*)p=active_area==(int)i?1:0;return 0;}
 assert(!"unexpected read address/size");return -1;
}
static int mock_write(const void*p,uint64_t a,size_t n){
 assert(cur_layout&&a==text+cur_layout->branch_off&&n==12);writes++;
 if((writes==1&&fail_write)||(writes==2&&fail_rollback)){memcpy(code,p,6);return -1;}
 if(writes==1&&write_corrupt){memcpy(code,p,n);code[0]^=1;return 0;}
 memcpy(code,p,n);return 0;
}
static void reset_for(uint32_t fw){
 const mb_layout*L=mb_find_layout(fw);
 cur_layout=L;firmware=fw;
 mock_data=L?text+L->data_delta:text+0xcb0000;
 memcpy(code,mb_original,12);
 games=active_area=bad_relay=writes=fail_write=fail_rollback=race=code_reads=reads=text_reads=0;
 bad_root=page_read_error=fail_code_read=write_corrupt=readback_error=0;
 locked_level=readonly_level=missing_level=large_level=-1;
 memory_target=1;memory_retry_blocked=0;memory_message[0]=0;
}
static void reset(void){reset_for(0x13200000);}
static void test_core(uint32_t fw){
 char msg[256];mb_snapshot s;
 reset_for(fw);assert(!mb_check(&s)&&s.state==MB_STOCK&&s.access_ready);
 assert(!mb_set(1,msg,sizeof msg)&&writes==1&&!memcmp(code,mb_eight,12));
 assert(!mb_set(1,msg,sizeof msg)&&writes==1);
 games=1;assert(!mb_set(1,msg,sizeof msg)&&writes==1);games=0;
 assert(!mb_set(0,msg,sizeof msg)&&writes==2&&!memcmp(code,mb_original,12));
 reset_for(fw);code[6]=0x38;assert(!mb_set(1,msg,sizeof msg)&&!memcmp(code,mb_eight,12));
 reset_for(fw);games=1;assert(mb_set(1,msg,sizeof msg)==2&&writes==0);
 reset_for(fw);games=-1;assert(mb_set(1,msg,sizeof msg)==2&&writes==0);
 reset_for(fw);active_area=2;assert(mb_set(1,msg,sizeof msg)==3&&writes==0);
 reset_for(fw);code[0]=0;assert(mb_set(1,msg,sizeof msg)==1&&writes==0);
 reset_for(fw);mock_data++;assert(mb_set(1,msg,sizeof msg)&&writes==0&&reads==0);
 for(int level=0;level<4;level++){
  reset_for(fw);locked_level=level;assert(mb_set(1,msg,sizeof msg)&&writes==0&&text_reads==0);
  reset_for(fw);readonly_level=level;assert(mb_set(1,msg,sizeof msg)&&writes==0&&text_reads==0);
  reset_for(fw);missing_level=level;assert(mb_set(1,msg,sizeof msg)&&writes==0&&text_reads==0);
 }
 reset_for(fw);bad_root=1;assert(mb_set(1,msg,sizeof msg)&&writes==0&&text_reads==0);
 reset_for(fw);page_read_error=1;assert(mb_set(1,msg,sizeof msg)&&writes==0&&text_reads==0);
 reset_for(fw);fail_code_read=1;assert(mb_set(1,msg,sizeof msg)&&writes==0);
 for(int level=1;level<3;level++){reset_for(fw);large_level=level;assert(!mb_set(1,msg,sizeof msg)&&writes==1);}
 reset_for(fw);large_level=0;assert(mb_set(1,msg,sizeof msg)&&text_reads==0&&writes==0);
 reset_for(fw);bad_relay=1;assert(mb_set(1,msg,sizeof msg)&&writes==0);
 reset_for(fw);fail_write=1;assert(mb_set(1,msg,sizeof msg)==5&&writes==2&&!memcmp(code,mb_original,12)&&strstr(msg,"verified"));
 reset_for(fw);fail_write=fail_rollback=1;assert(mb_set(1,msg,sizeof msg)==6&&writes==2&&strstr(msg,"FAILED"));
 reset_for(fw);write_corrupt=1;assert(mb_set(1,msg,sizeof msg)==5&&writes==2&&!memcmp(code,mb_original,12));
 reset_for(fw);readback_error=1;assert(mb_set(1,msg,sizeof msg)==5&&writes==2&&!memcmp(code,mb_original,12));
 reset_for(fw);race=1;assert(mb_set(1,msg,sizeof msg)==4&&writes==0);
}
int main(void){
 char msg[256];
 for(unsigned i=0;i<MB_LAYOUT_COUNT;i++){
  uint32_t fw=mb_layouts[i].fw;
  printf("Testing firmware 0x%08x...\n",fw);
  test_core(fw);
 }
 /* Unknown firmware rejected */
 reset();firmware=0x14000000;assert(mb_set(1,msg,sizeof msg)&&writes==0&&reads==0);
 /* Memory control policy (tested on 13.20 layout) */
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
 puts("PASS: all firmware layouts, access/page guards, enable/restore/rollback, auto/deferred/manual-off/retry policy.");
 return 0;
}
