#include "abox_log_ring.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int stamp(uint32_t tick,char *out,uint16_t cap)
{ (void)tick; return snprintf(out,cap,"2026-11-11 12:00:00")>0; }
static unsigned count_lines(const char *p)
{ unsigned n=0; while(*p) if(*p++=='\n') ++n; return n; }
int main(void)
{
    ABoxLogRing ring; char message[64],large[1536],small[976];
    ABoxLogRing_Init(&ring);
    for (unsigned i=0;i<32;i++) {
        snprintf(message,sizeof(message),"row%02u-abcdefghijklmnopqrstuvwxyz-0123456789",i);
        ABoxLogRing_PushText(&ring,i,2,"TEST",message);
    }
    unsigned a=ABoxLogRing_Dump(&ring,large,sizeof(large),stamp);
    unsigned b=ABoxLogRing_Dump(&ring,small,sizeof(small),stamp);
    assert(strstr(large,"row31-") && strstr(small,"row31-"));
    assert(count_lines(small)<count_lines(large));
    printf("1536: %u bytes %u lines; 976: %u bytes %u lines; newest row retained\n",a,count_lines(large),b,count_lines(small));
    return 0;
}
