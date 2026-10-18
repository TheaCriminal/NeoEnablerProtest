/* GPL-3.0-only. Budget extension state tracking.
 * Primary: DATA byte transaction (step hook, no HV).
 * Fallback: TEXT patch (HV required).
 * Called from the HTTP loop only: SDK kernel pipes are shared state.
 */
#pragma once
#include "neo_budget.h"

enum {MEM_MODE_NONE=0, MEM_MODE_DATA=1, MEM_MODE_TEXT=2};

static int memory_mode;
static mb_transaction memory_tx;
static const mb_layout *memory_layout;
static int memory_text_target;
static int memory_text_blocked;
static char memory_message[256]="8 GiB disabled (default)";

/* DATA byte path (step hook) */
static int memory_apply(const mb_layout *L, const mb_snapshot *s){
 memory_layout=L;
 int rc=mb_apply(L,&memory_tx,s);
 if(rc==MB_TX_OK)
  snprintf(memory_message,sizeof memory_message,"8 GiB enabled for PS4 games");
 else if(rc==MB_TX_REFUSED)
  snprintf(memory_message,sizeof memory_message,"8 GiB extension refused (preconditions not met)");
 else
  snprintf(memory_message,sizeof memory_message,"8 GiB extension write failed (rc=%d)",rc);
 return rc;
}

static int memory_restore(void){
 if(memory_mode==MEM_MODE_TEXT){
  memory_text_target=0;
  int rc=mb_text_set(0,memory_message,sizeof memory_message);
  return rc;
 }
 if(!memory_layout)return MB_TX_OK;
 int rc=mb_restore(memory_layout,&memory_tx);
 if(rc==MB_TX_OK)
  snprintf(memory_message,sizeof memory_message,"8 GiB disabled; original memory restored");
 else
  snprintf(memory_message,sizeof memory_message,"8 GiB restore FAILED (rc=%d); reboot recommended",rc);
 return rc;
}

static int memory_active(void){
 if(memory_mode==MEM_MODE_TEXT)return memory_text_target;
 return memory_tx.owned&&!memory_tx.restored;
}

/* TEXT/HV path */
static int memory_text_poll(void){
 if(memory_text_blocked)return memory_text_blocked==2?6:5;
 int rc=mb_text_set(memory_text_target,memory_message,sizeof memory_message);
 if(rc==5)memory_text_blocked=1;
 if(rc==6)memory_text_blocked=2;
 return rc;
}

static int memory_text_request(int enable){
 if(memory_text_blocked==2)return 6;
 memory_text_target=!!enable;memory_text_blocked=0;
 return memory_text_poll();
}
