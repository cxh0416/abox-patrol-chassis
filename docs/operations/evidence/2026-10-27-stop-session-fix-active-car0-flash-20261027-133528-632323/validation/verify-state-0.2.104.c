#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "abox_boot_v2.h"
#include "abox_boot_v2_boot.h"

static uint8_t pages[4096], app[0x36000];
static uint32_t app_size;
static unsigned jumps, installs, writes;

void ABoxBootV2_ImageBindLayoutInternal(const ABoxBootV2Layout *layout)
{
    (void)layout;
}

int ABox_PortFlashRead(uint32_t address, uint8_t *data, uint32_t length)
{
    if (address < 0x0803e800U || address + length > 0x0803f800U) return 0;
    memcpy(data, pages + address - 0x0803e800U, length);
    return 1;
}

int ABox_PortFlashBegin(void) { writes++; return 0; }
int ABox_PortFlashWrite(uint32_t a, const uint8_t *d, uint32_t n)
{
    (void)a; (void)d; (void)n; writes++; return 0;
}
int ABox_PortFlashErasePage(uint32_t a) { (void)a; writes++; return 0; }
void ABox_PortFlashEnd(void) {}
int ABoxBootV2_FaultValid(void) { return 0; }
void ABoxBootV2_FaultClear(void) { assert(!"CONFIRMED boot must not clear a fault"); }

static uint8_t valid_image(void *context, uint32_t size, uint32_t crc)
{
    uint32_t sp, reset;
    (void)context;
    memcpy(&sp, app, 4);
    memcpy(&reset, app + 4, 4);
    return size == app_size && crc == ABoxBootV2_Crc32(app, app_size) &&
        sp >= 0x20000000U && sp <= 0x20010000U && (reset & 1U) &&
        (reset & ~1U) >= 0x08008000U && (reset & ~1U) < 0x08008000U + app_size;
}

static ABoxBootV2InstallResult install(void *context, uint8_t slot,
                                        uint32_t size, uint32_t crc)
{
    (void)context; (void)slot; (void)size; (void)crc;
    installs++;
    return ABOX_BOOT_V2_INSTALL_FAILED;
}
static void jump(void *context) { (void)context; jumps++; }
static uint8_t reason(void *context) { (void)context; return 0; }

static void load(const char *name, uint8_t *data, size_t size)
{
    FILE *file = fopen(name, "rb");
    assert(file);
    assert(fread(data, 1U, size, file) == size);
    assert(fgetc(file) == EOF);
    fclose(file);
}

int main(int argc, char **argv)
{
    ABoxBootV2Layout layout = {
        0x0803e800U, 0x0803f000U, 0x800U, 0x08008000U,
        0x0803dfffU, 0x20000000U, 0x20010000U
    };
    ABoxBootV2BootPort port = {0};
    ABoxBootV2Record record;
    ABoxBootV2Diagnostics diagnostics;
    uint8_t original_pages[4096];
    FILE *file;

    assert(argc == 4);
    load(argv[1], pages, 2048U);
    load(argv[2], pages + 2048U, 2048U);
    file = fopen(argv[3], "rb");
    assert(file);
    app_size = (uint32_t)fread(app, 1U, sizeof(app), file);
    assert(feof(file));
    fclose(file);
    assert(app_size == 156900);

    port.flash_image_valid = valid_image;
    port.install_slot = install;
    port.jump_to_app = jump;
    port.reset_reason = reason;
    assert(ABoxBootV2_StateBindLayout(&layout));
    assert(ABoxBootV2Boot_Init(&port));
    assert(ABoxBootV2_StateLoad(&record));
    assert(ABoxBootV2_StateDiagnostics(&diagnostics));
    assert(record.sequence == 1U);
    assert(record.state == ABOX_BOOT_V2_CONFIRMED);
    assert(record.image_size == app_size);
    assert(record.image_crc32 == ABoxBootV2_Crc32(app, app_size));
    assert(!strcmp(record.image_version, "patrol-chassis-0.2.104"));
    assert(diagnostics.page_a_valid && !diagnostics.page_b_valid);
    assert(diagnostics.selected_page == 0U && diagnostics.selected_sequence == 1U);

    ABoxBootV2Boot_Task();
    assert(jumps == 1U && installs == 0U && writes == 0U);

    memcpy(original_pages, pages, sizeof(pages));
    pages[108] ^= 1U;
    jumps = installs = writes = 0U;
    ABoxBootV2Boot_Task();
    assert(jumps == 0U && installs == 0U && writes == 0U);

    memcpy(pages, original_pages, sizeof(pages));
    app[32] ^= 1U;
    jumps = installs = writes = 0U;
    ABoxBootV2Boot_Task();
    assert(jumps == 0U && installs == 0U && writes == 0U);
    puts("PASS: real Boot State V3 parser/CRC, State A binding, State B erase, matching-image jump and mismatch rejection; no install/write callbacks.");
    return 0;
}
