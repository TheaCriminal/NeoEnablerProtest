/* GPL-3.0-only. PS4-only CPU clock policy using the stock MP1 driver.
 * Mirror of gpu_control.h: apply on request/new game, with bounded recovery of sustained resets to the
 * captured baseline clock. Never chase ordinary SMU variation. Runs in the manager's HTTP thread.
 * The ceiling is the same on PS5 and PS5 Pro, so no model detection is needed.
 *
 * Backend (taken from the firmware 13.60 libkernel_sys / SceShellCore, which log them as MP1_IOCTL_GET_COREPSTATE and
 * MP1_IOCTL_SET_COREPSTATE, "CPU core#%d, Pstate %d"):
 *   GET 0xc00cae1e  struct{u32 core; u32 0; u32 value}   value = core clock in MHz (libkernel returns value*1e6 Hz)
 *   SET 0x8008ae1d  struct{u32 core_mask; u32 value}     the system sets one core at a time (mask = 1<<core)
 * GET struct layout (verified against SceShellCore and libkernel_sys 13.60):
 *   +0 core number (input)   +4 pstate value (same unit the SET takes; the shell compares it to its desired value)
 *   +8 live core clock in MHz (libkernel_sys returns this *1e6 Hz; the shell's CPU monitor prints it)
 * The live clock (+8) is what the UI shows and what the reset heuristic uses. The pstate (+4) is what restore writes
 * back, so a restore never feeds a live reading into SET. There is no fallback from +8 to +4: if +8 is not a
 * plausible MHz figure the read fails ("unavailable") instead of showing a stale request value as live.
 * The system has 8 cores (0..7). Nothing is written unless the baseline read succeeded.
 */
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#define CPU_MIN_MHZ 2850U       /* lowest selectable target: 2.85 GHz */
#define CPU_MAX_MHZ 3500U       /* 3.5 GHz ceiling, PS5 and PS5 Pro alike */
#define CPU_DEFAULT_MHZ 3000U
#define CPU_MAX_RECOVERIES 10U  /* automatic re-applies allowed before giving up */
#define CPU_STABLE_POLLS 10U    /* polls above baseline before the recovery budget is refunded */
#define CPU_CORES 8
#ifndef CPU_GET_IOCTL
#define CPU_GET_IOCTL 0xc00cae1eUL
#endif
#ifndef CPU_SET_IOCTL
#define CPU_SET_IOCTL 0x8008ae1dUL
#endif
static inline int cpu_target_valid(unsigned mhz){return mhz>=CPU_MIN_MHZ&&mhz<=CPU_MAX_MHZ&&mhz%50==0;}
typedef struct{
 int enabled,applied,owner_pid,baseline_valid,blocked,restore_pending,reading_valid;
 unsigned target,baseline,actual,requested,baseline_core[CPU_CORES],actions,reset_samples,recoveries,stable_samples;
 char message[192];
} cpu_control;
static int cpu_fail_core=-1,cpu_fail_errno;  /* where the last backend write failed, for the status text */
static unsigned cpu_last_core[CPU_CORES];    /* live clock (+8) per core from the last read */
static unsigned cpu_last_pstate[CPU_CORES];  /* pstate value (+4) per core from the last read, 0 = not reported */
static cpu_control cpu={.target=CPU_DEFAULT_MHZ,.message="CPU boost disabled"};
#ifndef NEO_CPU_TEST
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
static inline int cpu_backend_supported(void){return 1;}
/* Highest of the 8 live core clocks (MHz); core_out gets every core's live clock and cpu_last_pstate every core's
 * pstate value. A core whose live clock (+8) is not plausible fails the whole read. */
static inline int cpu_backend_read(unsigned*out,unsigned*core_out){
 int fd=open("/dev/mp1",O_RDONLY);if(fd<0)return -1;
 unsigned best=0,vals[CPU_CORES],ps[CPU_CORES];int bad=0;
 for(unsigned c=0;c<CPU_CORES&&!bad;c++){
  uint32_t q[3]={c,0,0};
  if(ioctl(fd,CPU_GET_IOCTL,q)){bad=1;break;}
  unsigned v=q[2];
  if(v<400||v>4000){bad=1;break;}
  vals[c]=v;ps[c]=(q[1]>=400&&q[1]<=4000)?q[1]:0;
  if(v>best)best=v;
 }
 close(fd);if(bad)return -1;
 memcpy(cpu_last_pstate,ps,sizeof ps);
 if(core_out)memcpy(core_out,vals,sizeof vals);
 *out=best;return 0;
}
static inline int cpu_backend_set_core(int fd,unsigned core,unsigned mhz){
 uint32_t q[2]={1u<<core,mhz};
 if(ioctl(fd,CPU_SET_IOCTL,q)){cpu_fail_core=(int)core;cpu_fail_errno=errno;return -1;}
 return 0;
}
/* Every core to one clock. Stops at the first refused write; cores written before it are the caller's to restore. */
static inline int cpu_backend_set(unsigned mhz){
 cpu_fail_core=-1;cpu_fail_errno=0;
 if(mhz<400||mhz>CPU_MAX_MHZ)return -1;
 int fd=open("/dev/mp1",O_RDWR);if(fd<0){cpu_fail_errno=errno;return -1;}
 int rc=0;for(unsigned c=0;c<CPU_CORES&&!rc;c++)rc=cpu_backend_set_core(fd,c,mhz);
 close(fd);return rc;
}
/* Every core back to the pstate value it had when the baseline was captured. Cores already there are not written,
 * so a driver that refuses writes still "restores" cleanly when nothing was ever changed. */
static inline int cpu_backend_restore(void){
 cpu_fail_core=-1;cpu_fail_errno=0;
 unsigned now[CPU_CORES],best=0;int have=!cpu_backend_read(&best,now);
 int fd=open("/dev/mp1",O_RDWR);if(fd<0){cpu_fail_errno=errno;return -1;}
 int rc=0;
 for(unsigned c=0;c<CPU_CORES;c++){
  if(!cpu.baseline_core[c])continue;
  if(have&&cpu_last_pstate[c]==cpu.baseline_core[c])continue;
  if(cpu_backend_set_core(fd,c,cpu.baseline_core[c]))rc=-1;
 }
 close(fd);return rc;
}
/* True when the driver answers a core read at all, whatever the values are. Keeps the CPU page reachable for
 * diagnostics even when the live clock field is not plausible. */
static inline int cpu_backend_responds(void){
 int fd=open("/dev/mp1",O_RDONLY);if(fd<0)return 0;
 uint32_t q[3]={0,0,0};int rc=ioctl(fd,CPU_GET_IOCTL,q);close(fd);return rc==0;
}
static inline void cpu_probe_add(char*out,size_t cap,size_t*at,const char*fmt,...){
 if(*at>=cap)return;
 va_list ap;va_start(ap,fmt);int n=vsnprintf(out+*at,cap-*at,fmt,ap);va_end(ap);
 if(n>0)*at=(*at+(size_t)n<cap)?*at+(size_t)n:cap-1;
}
/* Diagnostic text for the CPU clock backend. Read-only unless do_write is set. The write test only touches core 0:
 * it writes the pstate it already has, then 3200..3500 MHz, and always writes the original value back at the end. */
static inline int cpu_probe_report(char*out,size_t cap,int do_write){
 size_t at=0;out[0]=0;
 cpu_probe_add(out,cap,&at,"CPU probe (%s)\n",do_write?"with write test":"read only");
 int fd=open("/dev/mp1",do_write?O_RDWR:O_RDONLY);
 if(fd<0){cpu_probe_add(out,cap,&at,"open /dev/mp1 failed, errno %d\n",errno);return -1;}
 uint32_t m=0;int rc,e;
 m=0;errno=0;rc=ioctl(fd,0x4004b200UL,&m);e=errno;cpu_probe_add(out,cap,&at,"mode 4004b200: rc=%d errno=%d val=%u\n",rc,e,m);
 m=0;errno=0;rc=ioctl(fd,0x4004b202UL,&m);e=errno;cpu_probe_add(out,cap,&at,"flag 4004b202: rc=%d errno=%d val=%u\n",rc,e,m);
 unsigned p0=0;
 for(unsigned c=0;c<CPU_CORES;c++){
  uint32_t q[3]={c,0,0};errno=0;rc=ioctl(fd,CPU_GET_IOCTL,q);e=errno;
  cpu_probe_add(out,cap,&at,"core%u: rc=%d errno=%d +4(pstate)=%u +8(live)=%u\n",c,rc,e,q[1],q[2]);
  if(!c&&!rc)p0=q[1];
 }
 uint32_t w[27]={0};errno=0;rc=ioctl(fd,0x406cae01UL,w);e=errno;
 cpu_probe_add(out,cap,&at,"GET_CLK 406cae01: rc=%d errno=%d\n",rc,e);
 for(int i=0;i<27;i+=9)cpu_probe_add(out,cap,&at,"  [%2d..%2d] %u %u %u %u %u %u %u %u %u\n",i,i+8,w[i],w[i+1],w[i+2],w[i+3],w[i+4],w[i+5],w[i+6],w[i+7],w[i+8]);
 if(do_write){
  if(cpu.baseline_valid||cpu.applied)cpu_probe_add(out,cap,&at,"write test skipped: disable CPU boost first\n");
  else if(p0<400||p0>4000)cpu_probe_add(out,cap,&at,"write test skipped: core0 pstate not readable\n");
  else{
   unsigned list[]={p0,3200,3300,3400,3500};
   for(int i=0;i<5;i++){
    unsigned v=list[i];int dup=0;for(int j=0;j<i;j++)if(list[j]==v)dup=1;
    if(dup)continue;
    uint32_t s[2]={1,v};errno=0;rc=ioctl(fd,CPU_SET_IOCTL,s);e=errno;usleep(100000);
    uint32_t q[3]={0,0,0};ioctl(fd,CPU_GET_IOCTL,q);
    cpu_probe_add(out,cap,&at,"SET core0=%u: rc=%d errno=%d -> +4=%u +8=%u\n",v,rc,e,q[1],q[2]);
   }
   uint32_t s[2]={1,p0};errno=0;rc=ioctl(fd,CPU_SET_IOCTL,s);e=errno;usleep(100000);
   uint32_t q[3]={0,0,0};ioctl(fd,CPU_GET_IOCTL,q);
   cpu_probe_add(out,cap,&at,"restore core0=%u: rc=%d errno=%d -> +4=%u +8=%u\n",p0,rc,e,q[1],q[2]);
  }
 }
 close(fd);return 0;
}
#endif
static inline void cpu_configure(int enable,unsigned mhz){
 cpu.enabled=!!enable&&cpu_backend_supported()&&cpu_target_valid(mhz);
 cpu.target=cpu_target_valid(mhz)?mhz:CPU_DEFAULT_MHZ;
}
static inline int cpu_refresh(void){
 unsigned actual=0;memset(cpu_last_pstate,0,sizeof cpu_last_pstate);
 cpu.reading_valid=!cpu_backend_read(&actual,cpu_last_core);
 if(cpu.reading_valid){
  unsigned req=0;
  for(unsigned c=0;c<CPU_CORES;c++){
   if(!cpu_last_pstate[c])cpu_last_pstate[c]=cpu_last_core[c];  /* backend reported no pstate: use the live clock */
   if(cpu_last_pstate[c]>req)req=cpu_last_pstate[c];
  }
  cpu.actual=actual;cpu.requested=req;
 }
 return cpu.reading_valid?0:-1;
}
static inline int cpu_restore(void){
 if(!cpu.baseline_valid){cpu.applied=0;cpu.restore_pending=0;return 0;}
 cpu.actions++;
 if(cpu_backend_restore()){
  cpu.blocked=1;cpu.restore_pending=1;
  snprintf(cpu.message,sizeof cpu.message,"Clock restoration failed (core %d, errno %d). Retry Disable before enabling boost.",cpu_fail_core,cpu_fail_errno);return -3;
 }
 unsigned previous=cpu.baseline;
 cpu.baseline_valid=0;cpu.applied=0;cpu.owner_pid=0;cpu.restore_pending=0;cpu.blocked=0;
 cpu_refresh();
 snprintf(cpu.message,sizeof cpu.message,"Boost off; previous clock requested: %u MHz",previous);return 0;
}
static inline void cpu_owner_changed(int pid){
 if(pid>=0&&cpu.owner_pid&&pid!=cpu.owner_pid&&!cpu.restore_pending){
  /* A launch/exit changes the system power policy. Do not replay the old game's
   * baseline over the new system policy; take a new baseline for the new game. */
  cpu.baseline_valid=0;cpu.applied=0;cpu.owner_pid=0;cpu.blocked=0;
  cpu.reset_samples=0;cpu.recoveries=0;cpu.stable_samples=0;
 }
}
static inline int cpu_poll(int pid){
 if(!cpu_backend_supported()){
  cpu.reading_valid=0;snprintf(cpu.message,sizeof cpu.message,"CPU clock control unavailable");return -1;
 }
 cpu_owner_changed(pid);
 if(cpu.restore_pending)return -3;
 if(cpu.blocked)return -2;
 if(cpu_refresh()){
  cpu.reset_samples=0;
  snprintf(cpu.message,sizeof cpu.message,"SMU clock query failed; no new clock request sent");return -1;
 }
 if(!cpu.enabled){snprintf(cpu.message,sizeof cpu.message,"CPU boost disabled");return 0;}
 if(pid<=0){
  cpu.reset_samples=0;
  snprintf(cpu.message,sizeof cpu.message,pid<0?"Game detection unavailable; waiting":"Enabled; waiting for a PS4 game");return 1;
 }
 /* Two consecutive policy polls back at or below the captured baseline clock.
  * Unlike the GPU there is no fixed "standard" value, so the baseline captured before the first apply is the
  * reset marker. This is a reset heuristic, not a foreground or thermal-state measurement.
  * At most CPU_MAX_RECOVERIES automatic recoveries per game/manual enable; never retry a
  * failed SET, failed read, or ambiguous process scan. */
 if(cpu.applied&&cpu.baseline_valid&&cpu.target>cpu.baseline&&cpu.actual<=cpu.baseline){
  cpu.stable_samples=0;
  if(++cpu.reset_samples>=2){
   cpu.reset_samples=0;
   if(cpu.recoveries>=CPU_MAX_RECOVERIES){
    cpu.blocked=1;
    snprintf(cpu.message,sizeof cpu.message,"Repeated clock resets; automatic recovery stopped. Check game/system state before retrying Enable.");
    return -2;
   }
   cpu.recoveries++;cpu.applied=0;
  }
 }else{
  cpu.reset_samples=0;
  /* Boost held for a while: earlier resets were separate events, so give the recovery budget back.
   * Rapid back-to-back resets never reach CPU_STABLE_POLLS and still hit the limit above. */
  if(cpu.applied&&cpu.baseline_valid&&cpu.actual>cpu.baseline){
   if(++cpu.stable_samples>=CPU_STABLE_POLLS){cpu.recoveries=0;cpu.stable_samples=0;}
  }else cpu.stable_samples=0;
 }
 if(!cpu.applied){
  if(!cpu.baseline_valid){cpu.baseline=cpu.actual;memcpy(cpu.baseline_core,cpu_last_pstate,sizeof cpu.baseline_core);cpu.baseline_valid=1;cpu.owner_pid=pid;}
  cpu.actions++;
  if(cpu_backend_set(cpu.target)){
   int fcore=cpu_fail_core,ferr=cpu_fail_errno;
   int restored=cpu_restore();cpu.blocked=1;
   if(!restored)snprintf(cpu.message,sizeof cpu.message,"Boost request refused by the driver (core %d, errno %d); previous clock kept. Retry Enable.",fcore,ferr);
   return restored?-3:-2;
  }
  cpu.applied=1;cpu_refresh();
 }
 if(cpu.reading_valid)snprintf(cpu.message,sizeof cpu.message,"Target %u MHz requested for the running PS4 game",cpu.target);
 else snprintf(cpu.message,sizeof cpu.message,"Target %u MHz requested; SMU reading unavailable",cpu.target);
 return 0;
}
static inline int cpu_request(int enable,unsigned mhz,int pid){
 if(enable&&(!cpu_target_valid(mhz)||!cpu_backend_supported()))return -1;
 cpu_owner_changed(pid);
 if(!enable){
  cpu.enabled=0;int retry=cpu.restore_pending;int rc=cpu_restore();
  if(rc&&retry){
   /* The driver refused the restore twice. Keep the manager usable (Quit, Enable) instead of failing forever;
    * the clocks go back to normal on their own at the next game launch. */
   cpu.baseline_valid=0;cpu.applied=0;cpu.owner_pid=0;cpu.restore_pending=0;cpu.blocked=0;
   snprintf(cpu.message,sizeof cpu.message,"Could not restore the CPU clock (core %d, errno %d); boost state cleared. The clock returns to normal at the next game launch.",cpu_fail_core,cpu_fail_errno);
   return 0;
  }
  return rc;
 }
 if(cpu.restore_pending){snprintf(cpu.message,sizeof cpu.message,"Retry Disable to restore the previous clock first");return -3;}
 cpu.enabled=1;cpu.target=mhz;cpu.blocked=0;cpu.applied=0;cpu.reset_samples=0;cpu.recoveries=0;cpu.stable_samples=0;
 return cpu_poll(pid);
}
static inline const char* cpu_state_text(void){
 if(!cpu_backend_supported())return "unavailable";
 if(cpu.restore_pending)return "restore failed";
 if(cpu.blocked)return "request failed";
 if(!cpu.enabled)return "disabled";
 return cpu.applied?"enabled":"waiting";
}
/* Accept exactly one unescaped decimal MHz field from a small HTML form. */
static inline int cpu_form_mhz(const char*body,unsigned*out){
 int found=0;const char*p=body;
 while(*p){
  const char*end=strchr(p,'&');if(!end)end=p+strlen(p);
  if((size_t)(end-p)>=4&&!memcmp(p,"mhz=",4)){
   if(found||end==p+4)return -1;
   unsigned value=0;
   for(const char*q=p+4;q<end;q++){if(*q<'0'||*q>'9'||value>10000)return -1;value=value*10+(unsigned)(*q-'0');}
   if(!cpu_target_valid(value))return -1;
   *out=value;found=1;
  }
  p=*end?end+1:end;
 }
 return found;
}
