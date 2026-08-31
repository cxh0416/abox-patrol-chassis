#ifndef ABOX_BOOT_TEST_H
#define ABOX_BOOT_TEST_H

#include <stdint.h>

enum {
    BOOT_TEST_PHASE_NONE = 0U,
    BOOT_TEST_PHASE_STATE_WRITE = 1U,
    BOOT_TEST_PHASE_POST_ERASE_STATE = 2U,
};

/* These are intentionally compile-time no-ops in production/App builds. */
#ifdef ABOX_BOOT_FAULT_INJECTION_TEST

void BootTest_SetPhase(uint32_t phase);
void BootTest_BootStateCheckpoint(uint32_t address, uint32_t offset, uint32_t record_size,
                                  uint32_t image_size, uint32_t image_crc32, const char *image_version);
void BootTest_ProgramCheckpoint(uint32_t total, uint32_t size, uint32_t crc);
uint8_t BootTest_ForceVectorInvalid(uint32_t size, uint32_t crc);
uint8_t BootTest_CorruptBothStatePages(uint32_t image_size, uint32_t image_crc32, const char *image_version);
uint8_t BootTest_UfsCandidateMissingRequested(uint32_t image_size, uint32_t image_crc32,
                                               const char *image_version);
uint8_t BootTest_UfsCleanupRequested(void);
#else
static inline void BootTest_SetPhase(uint32_t phase) { (void)phase; }
static inline void BootTest_BootStateCheckpoint(uint32_t address, uint32_t offset, uint32_t record_size,
                                                uint32_t image_size, uint32_t image_crc32,
                                                const char *image_version)
{
    (void)address; (void)offset; (void)record_size; (void)image_size; (void)image_crc32; (void)image_version;
}
static inline void BootTest_ProgramCheckpoint(uint32_t total, uint32_t size, uint32_t crc)
{
    (void)total; (void)size; (void)crc;
}
static inline uint8_t BootTest_ForceVectorInvalid(uint32_t size, uint32_t crc)
{
    (void)size; (void)crc; return 0U;
}
static inline uint8_t BootTest_CorruptBothStatePages(uint32_t image_size, uint32_t image_crc32, const char *image_version)
{
    (void)image_size; (void)image_crc32; (void)image_version; return 0U;
}
static inline uint8_t BootTest_UfsCandidateMissingRequested(uint32_t image_size, uint32_t image_crc32,
                                                             const char *image_version)
{
    (void)image_size; (void)image_crc32; (void)image_version; return 0U;
}
static inline uint8_t BootTest_UfsCleanupRequested(void) { return 0U; }
#endif

#endif
