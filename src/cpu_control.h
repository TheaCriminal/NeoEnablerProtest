/* GPL-3.0-only. PS4-only CPU clock policy using the stock MP1 driver.
 * Mirror of gpu_control.h: apply on request/new game, with bounded recovery of sustained resets to the
 * captured baseline clock. Never chase ordinary SMU variation. Runs in the manager's HTTP thread.
 * The ceiling is the same on PS5 and PS5 Pro, so no model detection is needed.
 *
 * Backend (taken from the firmware 13.60 libkernel_sys / SceShellCore, which log them as MP1_IOCTL_GET_COREPSTATE and
 * MP1_IOCTL_SET_COREPSTATE, "CPU core#%d, Pstate %d"):
 *   GET 0xc00cae1e  struct{u32 core; u32 0; u32 value}   value = core clock in MHz (libkernel returns value*1e6 Hz)
 *   SET 0x8008ae1d  struct{u32 core_mask; u32 value}     the system sets one core at a time (mask = 1<<core)
 * The system has 8 cores (0..7). Values are read back and range-checked, so if a firmware answers with
 * something that is not a plausible MHz figure the backend reports "unavailable" and nothing is written.
 */
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
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
 unsigned target,baseline,actual,baseline_core[CPU_CORES],actions,reset_samples,recoveries,stable_samples;
 char message[192];
} cpu_control;
static int cpu_fail_core=-1,cpu_fail_errno;  /* where the last backend write failed, for the status text */
static cpu_control cpu={.target=CPU_DEFAULT_MHZ,.message="CPU boost disabled"};
#ifndef NEO_CPU_TEST
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
static inline int cpu_backend_supported(void){return 1;}
/* Highest of the 8 core clocks (MHz) and, when core_out is given, every core's clock. */
static inline int cpu_backend_read(unsigned*out,unsigned*core_out){
 int fd=open("/dev/mp1",O_RDONLY);if(fd<0)return -1;
 unsigned best=0,vals[CPU_CORES];int bad=0;
 for(unsigned c=0;c<CPU_CORES&&!bad;c++){
  uint32_t q[3]={c,0,0};
  if(ioctl(fd,CPU_GET_IOCTL,q)){bad=1;break;}
  unsigned v=q[2];if(v<400||v>4000)v=q[1];  /* value is at +8; +4 kept as a defensive fallback */
  if(v<400||v>4000){bad=1;break;}
  vals[c]=v;if(v>best)best=v;
 }
 close(fd);if(bad)return -1;
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
/* Every core back to the clock it had when the baseline was captured. Cores already there are not written,
 * so a driver that refuses writes still "restores" cleanly when nothing was ever changed. */
static inline int cpu_backend_restore(void){
 cpu_fail_core=-1;cpu_fail_errno=0;
 unsigned now[CPU_CORES],best=0;int have=!cpu_backend_read(&best,now);
 int fd=open("/dev/mp1",O_RDWR);if(fd<0){cpu_fail_errno=errno;return -1;}
 int rc=0;
 for(unsigned c=0;c<CPU_CORES;c++){
  if(have&&now[c]==cpu.baseline_core[c])continue;
  if(cpu_backend_set_core(fd,c,cpu.baseline_core[c]))rc=-1;
 }
 close(fd);return rc;
}
#endif
static inline void cpu_configure(int enable,unsigned mhz){
 cpu.enabled=!!enable&&cpu_backend_supported()&&cpu_target_valid(mhz);
 cpu.target=cpu_target_valid(mhz)?mhz:CPU_DEFAULT_MHZ;
}
static unsigned cpu_last_core[CPU_CORES];
static inline int cpu_refresh(void){
 unsigned actual=0;cpu.reading_valid=!cpu_backend_read(&actual,cpu_last_core);
 if(cpu.reading_valid)cpu.actual=actual;
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
  if(!cpu.baseline_valid){cpu.baseline=cpu.actual;memcpy(cpu.baseline_core,cpu_last_core,sizeof cpu.baseline_core);cpu.baseline_valid=1;cpu.owner_pid=pid;}
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
