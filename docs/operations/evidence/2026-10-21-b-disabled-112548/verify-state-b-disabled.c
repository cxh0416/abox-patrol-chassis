#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "abox_boot_v2.h"
#include "abox_boot_v2_boot.h"
static uint8_t pages[4096], app[0x36000];
static uint32_t app_size;
static unsigned jumps, installs, writes;
void ABoxBootV2_ImageBindLayoutInternal(const ABoxBootV2Layout *layout) { (void)layout; }
int ABox_PortFlashRead(uint32_t address,uint8_t *data,uint32_t length) {
    if(address<0x0803e800U || address+length>0x0803f800U)return 0;
    memcpy(data,pages+address-0x0803e800U,length); return 1;
}
int ABox_PortFlashBegin(void) { writes++; return 0; }
int ABox_PortFlashWrite(uint32_t a,const uint8_t *d,uint32_t n) { (void)a;(void)d;(void)n;writes++;return 0; }
int ABox_PortFlashErasePage(uint32_t a) { (void)a; writes++; return 0; }
void ABox_PortFlashEnd(void) {}
int ABoxBootV2_FaultValid(void) { return 0; }
void ABoxBootV2_FaultClear(void) { assert(!"NORMAL must not enter trial recovery"); }
static uint8_t valid_image(void *c,uint32_t size,uint32_t crc) {
    uint32_t sp,reset;(void)c;memcpy(&sp,app,4);memcpy(&reset,app+4,4);
    return size==app_size && crc==ABoxBootV2_Crc32(app,app_size) &&
        sp>=0x20000000U && sp<=0x20010000U && (reset&1U) &&
        (reset&~1U)>=0x08008000U && (reset&~1U)<0x08008000U+app_size;
}
static ABoxBootV2InstallResult install(void *c,uint8_t s,uint32_t n,uint32_t crc)
{ (void)c;(void)s;(void)n;(void)crc;installs++;return ABOX_BOOT_V2_INSTALL_FAILED; }
static void jump(void *c) { (void)c;jumps++; }
static uint8_t reason(void *c) { (void)c;return 0; }
static void load(const char *name,uint8_t *data,size_t size) {
    FILE *f=fopen(name,"rb");assert(f);assert(fread(data,1,size,f)==size);assert(fgetc(f)==EOF);fclose(f);
}
static void check(uint32_t seq) {
    ABoxBootV2Record r;ABoxBootV2Diagnostics d;
    assert(ABoxBootV2_StateLoad(&r));assert(ABoxBootV2_StateDiagnostics(&d));
    assert(r.sequence==seq && r.state==ABOX_BOOT_V2_NORMAL && r.candidate_slot==255);
    assert(r.image_size==app_size && !strcmp(r.image_version,"patrol-chassis-0.2.68"));
    assert(r.stable_image_size==151048 && !strcmp(r.stable_image_version,"patrol-chassis-0.2.65"));
    jumps=0;ABoxBootV2Boot_Task();assert(jumps==1 && installs==0 && writes==0);
}
int main(int argc,char **argv) {
    ABoxBootV2Layout layout={0x0803e800U,0x0803f000U,0x800U,0x08008000U,0x0803dfffU,0x20000000U,0x20010000U};
    ABoxBootV2BootPort port={0};uint8_t saved[4096];FILE *f;
    assert(argc==4);load(argv[1],pages,2048);load(argv[2],pages+2048,2048);
    f=fopen(argv[3],"rb");assert(f);app_size=(uint32_t)fread(app,1,sizeof(app),f);assert(feof(f));fclose(f);
    assert(app_size==151008);memcpy(saved,pages,sizeof(pages));
    port.flash_image_valid=valid_image;port.install_slot=install;port.jump_to_app=jump;port.reset_reason=reason;
    assert(ABoxBootV2_StateBindLayout(&layout));assert(ABoxBootV2Boot_Init(&port));check(24);
    memset(pages+2048,255,2048);check(23);
    memcpy(pages,saved,sizeof(pages));memset(pages,255,2048);check(24);
    memcpy(pages,saved,sizeof(pages));pages[2048+108]^=1;check(23);
    memcpy(pages,saved,sizeof(pages));app[32]^=1;jumps=0;ABoxBootV2Boot_Task();assert(jumps==0);
    puts("PASS: real Boot State V3 parser/CRC, highest-sequence selection, A/B alone, corrupt-page fallback, matching-image jump and mismatched-image rejection; no install/write callbacks.");
}
