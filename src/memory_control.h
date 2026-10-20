/* GPL-3.0-only. Called from the HTTP loop only: SDK kernel pipes are shared
 * state, so a background polling thread must not race HTTP kernel accesses.
 */
#pragma once
#include "neo_budget.h"
static int memory_target=0;
static int memory_retry_blocked;
static char memory_message[256]="8GB Mode disabled (default)";
static int memory_text_poll(void){
 if(memory_retry_blocked)return memory_retry_blocked==2?6:5;
 int rc=mb_set(memory_target,memory_message,sizeof memory_message);
 if(rc==5)memory_retry_blocked=1;
 if(rc==6)memory_retry_blocked=2;
 return rc;
}
static int memory_text_request(int enable){
 if(memory_retry_blocked==2)return 6;
 memory_target=!!enable;memory_retry_blocked=0;
 return memory_text_poll();
}

#ifdef NEO_BUDGET_TEST
#define memory_poll memory_text_poll
#define memory_request memory_text_request
#else
#include "budget_mount_control.h"
#endif
