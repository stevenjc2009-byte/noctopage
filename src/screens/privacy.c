
#include "screens/privacy.h"
#include "core/pin.h"
#include "core/prefs.h"
#include "ui/osk.h"
#include "app.h"
#include <psp2/kernel/rng.h>
#include <string.h>
static int unlocked,failed;
static uint64_t last_active,blocked_until;
static void wipe(char *s,size_t n) { volatile char *v=s; while(n--) *v++=0; }
int privacy_is_unlocked(void) { return unlocked; }
void privacy_lock(void) { unlocked=0; }
static int setup(void)
{
 char a[16]={0},b[16]={0},encoded[97]; unsigned char salt[16]; int ok=0;
 if(osk_prompt("Create a Private PIN (4-8 digits)","",OSK_PASSWORD,a,sizeof a)!=1) goto done;
 if(!pin_valid(a)) { app_message("PIN needs 4-8 digits"); goto done; }
 if(osk_prompt("Confirm Private PIN","",OSK_PASSWORD,b,sizeof b)!=1) goto done;
 if(strcmp(a,b)) { app_message("PINs do not match"); goto done; }
 if(sceKernelGetRandomNumber(salt,sizeof salt)<0 || pin_encode(a,salt,encoded)<0) { app_message("Could not create PIN"); goto done; }
 if(prefs_set_checked("private","pin",encoded)<0) { app_message("Could not save PIN. Check available storage"); goto done; } ok=1;
done: wipe(a,sizeof a); wipe(b,sizeof b); return ok;
}
int privacy_unlock(uint64_t now)
{
 if(unlocked) { last_active=now; return 1; }
 if(now<blocked_until) { app_message("Too many attempts. Try again in 30 seconds"); return 0; }
 const char *encoded=prefs_get("private","pin","");
 if(!*encoded) { if(!setup()) return 0; }
 else {
  char pin[16]={0}; int result=osk_prompt("Unlock Private","",OSK_PASSWORD,pin,sizeof pin);
  int ok=result==1 && pin_verify(pin,encoded); wipe(pin,sizeof pin);
  if(!ok) { if(result==1) { app_message("Incorrect PIN"); if(++failed>=5) { blocked_until=now+30000; failed=0; } } return 0; }
 }
 failed=0; unlocked=1; last_active=now; return 1;
}
int privacy_tick(uint64_t now,int active)
{
 if(unlocked && now-last_active>=120000) { privacy_lock(); return 1; }
 if(unlocked && active) last_active=now;
 return 0;
}
void privacy_change(uint64_t now)
{
 privacy_lock(); if(privacy_unlock(now) && setup()) app_message("Private PIN changed");
 privacy_lock();
}
