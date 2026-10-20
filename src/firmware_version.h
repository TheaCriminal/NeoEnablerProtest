/* GPL-3.0-only. Actual firmware is immutable for the manager's lifetime.
 * Cache only a successful, complete query. Never replace a known version with
 * zero after a transient sysctl failure, and never substitute the SDK version.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#ifndef NEO_FW_SYSCTL
#include <sys/sysctl.h>
#define NEO_FW_SYSCTL sysctlbyname
#endif
static uint32_t fw_cached_raw;
static uint32_t fw_raw_version(void){
 if(!fw_cached_raw){
  uint32_t raw=0;size_t n=sizeof raw;
  if(!NEO_FW_SYSCTL("machdep.upd_version",&raw,&n,NULL,0)&&n==sizeof raw&&raw)
   fw_cached_raw=raw;
 }
 return fw_cached_raw;
}
