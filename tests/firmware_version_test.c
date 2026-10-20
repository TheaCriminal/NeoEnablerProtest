/* Actual version discovery across failures and idle/gameplay transitions. */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
static int calls,fail,short_read;
static uint32_t actual;
static int query(const char *name,void *out,size_t *n,const void *in,size_t len){
 assert(!strcmp(name,"machdep.upd_version")&&!in&&!len&&*n==4);
 calls++;memcpy(out,&actual,4);if(short_read)*n=2;return fail?-1:0;
}
#define NEO_FW_SYSCTL query
#include "../src/firmware_version.h"
int main(void){
 /* A failed call may still modify its output; never cache that value. */
 actual=0x13600000;fail=1;assert(fw_raw_version()==0&&calls==1);
 fail=0;short_read=1;assert(fw_raw_version()==0&&calls==2);
 short_read=0;actual=0;assert(fw_raw_version()==0&&calls==3);
 actual=0x13600000;assert(fw_raw_version()==actual&&calls==4);
 /* After recognition, later query failures cannot change idle support. */
 fail=1;actual=0;assert(fw_raw_version()==0x13600000&&calls==4);
 assert(fw_raw_version()==0x13600000&&calls==4);
 /* A new manager queries its own firmware; no SDK-version substitution. */
 fw_cached_raw=0;fail=0;actual=0x13200000;
 assert(fw_raw_version()==0x13200000&&calls==5);
 fw_cached_raw=0;actual=0x14000000;
 assert(fw_raw_version()==0x14000000&&calls==6);
 puts("PASS: actual firmware query failure, short read, recovery and stable idle/gameplay identity");
}
