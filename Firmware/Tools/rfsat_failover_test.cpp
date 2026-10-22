// Host unit test for NtripFailover.ino. Build and run from Firmware/Tools:
//   g++ -std=c++17 -o /tmp/ft rfsat_failover_test.cpp && /tmp/ft
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdarg>
static uint32_t now=0; uint32_t millis(){return now;}
void systemPrintf(const char*f,...){va_list a;va_start(a,f);vprintf(f,a);va_end(a);}
void systemPrintln(const char*s){puts(s);}
struct S{ char ntripClient_CasterHost[50]; uint16_t ntripClient_CasterPort; char ntripClient_CasterUser[50],ntripClient_CasterUserPW[50],ntripClient_MountPoint[50];
 bool ntripClientFailover=true; uint16_t ntripClientFailbackInterval_s=600;
 char ntripClientAlt1_CasterHost[50]; uint16_t ntripClientAlt1_CasterPort; char ntripClientAlt1_CasterUser[50],ntripClientAlt1_CasterUserPW[50],ntripClientAlt1_MountPoint[50];
 char ntripClientAlt2_CasterHost[50]; uint16_t ntripClientAlt2_CasterPort; char ntripClientAlt2_CasterUser[50],ntripClientAlt2_CasterUserPW[50],ntripClientAlt2_MountPoint[50];
 char ntripClientAlt3_CasterHost[50]; uint16_t ntripClientAlt3_CasterPort; char ntripClientAlt3_CasterUser[50],ntripClientAlt3_CasterUserPW[50],ntripClientAlt3_MountPoint[50];} settings{};
#define NTRIP_CLIENT_SOURCES 4
typedef struct {char*host;uint16_t*port;char*user;char*userPw;char*mount;} NTRIP_SOURCE;
typedef enum{NTRIP_FAILOVER_NONE=0,NTRIP_FAILOVER_SWITCHED,NTRIP_FAILOVER_BACKOFF} NTRIP_FAILOVER_RESULT;
NTRIP_SOURCE *ntripSrc(); uint8_t ntripSourceActiveIndex(); uint8_t ntripSourceFirstUsable();
#include "../RTK_Everywhere/NtripFailover.ino"
const char*R[]={"NONE","SWITCHED","BACKOFF"};
int fails=0;
#define CHECK(c) do{ if(!(c)){printf("FAIL line %d: %s\n",__LINE__,#c);fails++;} }while(0)
int main(){
 strcpy(settings.ntripClient_CasterHost,"base.local"); settings.ntripClient_CasterPort=2101; strcpy(settings.ntripClient_MountPoint,"OWN");
 // single source -> NONE
 CHECK(ntripSourceFailover(false,"x")==NTRIP_FAILOVER_NONE);
 strcpy(settings.ntripClientAlt1_CasterHost,"gr.nrtk.eu"); settings.ntripClientAlt1_CasterPort=7801; strcpy(settings.ntripClientAlt1_MountPoint,"MSM_VRS");
 strcpy(settings.ntripClientAlt3_CasterHost,"euref-ip.net"); settings.ntripClientAlt3_CasterPort=2101; strcpy(settings.ntripClientAlt3_MountPoint,"XXX0");
 ntripSourceReset(true);
 CHECK(ntripSourceActiveIndex()==0); CHECK(ntripSourceValidCount()==3);
 auto r=ntripSourceFailover(false,"connect failed"); CHECK(r==NTRIP_FAILOVER_SWITCHED); CHECK(ntripSourceActiveIndex()==1);
 r=ntripSourceFailover(false,"no response"); CHECK(r==NTRIP_FAILOVER_SWITCHED); CHECK(ntripSourceActiveIndex()==3); // skips empty alt2
 r=ntripSourceFailover(false,"no data"); CHECK(r==NTRIP_FAILOVER_BACKOFF); CHECK(ntripSourceActiveIndex()==0);
 // success resets cycle
 r=ntripSourceFailover(false,"a"); CHECK(ntripSourceActiveIndex()==1); now=1000; ntripSourceConnected();
 CHECK(!ntripSourceFailbackDue()); now=1000+600000; CHECK(ntripSourceFailbackDue());
 ntripSourceReset(false); CHECK(ntripSourceActiveIndex()==0);
 // hard failure on primary disables it
 r=ntripSourceFailover(true,"unauthorized"); CHECK(r==NTRIP_FAILOVER_SWITCHED); CHECK(ntripSourceActiveIndex()==1);
 CHECK(ntripSourceFirstUsable()==1); ntripSourceConnected(); now+=700000; CHECK(!ntripSourceFailbackDue()); // primary disabled: no failback
 r=ntripSourceFailover(true,"sourcetable"); CHECK(ntripSourceActiveIndex()==3);
 r=ntripSourceFailover(false,"no data"); CHECK(r==NTRIP_FAILOVER_BACKOFF); CHECK(ntripSourceActiveIndex()==3); // only one usable
 r=ntripSourceFailover(true,"401"); CHECK(r==NTRIP_FAILOVER_NONE); // all disabled -> shutdown
 ntripSourceReset(true); CHECK(ntripSourceUsableCount()==3); CHECK(ntripSourceActiveIndex()==0);
 // failover off: alternates ignored
 settings.ntripClientFailover=false; CHECK(ntripSourceValidCount()==1); CHECK(ntripSourceFailover(false,"x")==NTRIP_FAILOVER_NONE);
 // primary empty but alt valid -> starts on alt
 settings.ntripClientFailover=true; settings.ntripClient_CasterHost[0]=0; ntripSourceReset(true); CHECK(ntripSourceActiveIndex()==1);
 ntripSourcePrintStatus();
 printf(fails? "%d FAILURES\n":"ALL TESTS PASSED\n",fails); return fails;
}
