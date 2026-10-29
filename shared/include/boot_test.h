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

// 保留关闭状态的 Boot 测试阶段钩子。当前兼容实现仅忽略参数，不修改正式 Boot 行为。
void BootTest_SetPhase(uint32_t phase);
// 保留关闭状态的 Boot State 写入故障注入检查点。当前实现不操作页面、镜像或版本参数，不产生故障注入。
void BootTest_BootStateCheckpoint(uint32_t address, uint32_t offset, uint32_t record_size,
                                  uint32_t image_size, uint32_t image_crc32, const char *image_version);
// 保留关闭状态的镜像编程检查点。当前实现不根据下载进度或 CRC 中断烧写。
void BootTest_ProgramCheckpoint(uint32_t total, uint32_t size, uint32_t crc);
// 提供不启用向量破坏测试的兼容查询。固定返回零，不会让有效 App 向量被测试路径判为无效。
uint8_t BootTest_ForceVectorInvalid(uint32_t size, uint32_t crc);
// 提供不启用双状态页破坏测试的兼容入口。固定返回零，当前实现不会擦除或修改 Boot State A/B。
uint8_t BootTest_CorruptBothStatePages(uint32_t image_size, uint32_t image_crc32, const char *image_version);
// 提供不启用 UFS 候选丢失注入的兼容查询。固定返回零，不删除模块文件或改变候选状态。
uint8_t BootTest_UfsCandidateMissingRequested(uint32_t image_size, uint32_t image_crc32,
                                               const char *image_version);
// 提供不启用 UFS 清理测试的兼容查询。固定返回零，当前实现不安排文件清理。
uint8_t BootTest_UfsCleanupRequested(void);
#else
// 保留关闭状态的 Boot 测试阶段钩子。当前兼容实现仅忽略参数，不修改正式 Boot 行为。
static inline void BootTest_SetPhase(uint32_t phase) { (void)phase; }
// 保留关闭状态的 Boot State 写入故障注入检查点。当前实现不操作页面、镜像或版本参数，不产生故障注入。
static inline void BootTest_BootStateCheckpoint(uint32_t address, uint32_t offset, uint32_t record_size,
                                                uint32_t image_size, uint32_t image_crc32,
                                                const char *image_version)
{
    (void)address; (void)offset; (void)record_size; (void)image_size; (void)image_crc32; (void)image_version;
}
// 保留关闭状态的镜像编程检查点。当前实现不根据下载进度或 CRC 中断烧写。
static inline void BootTest_ProgramCheckpoint(uint32_t total, uint32_t size, uint32_t crc)
{
    (void)total; (void)size; (void)crc;
}
// 提供不启用向量破坏测试的兼容查询。固定返回零，不会让有效 App 向量被测试路径判为无效。
static inline uint8_t BootTest_ForceVectorInvalid(uint32_t size, uint32_t crc)
{
    (void)size; (void)crc; return 0U;
}
// 提供不启用双状态页破坏测试的兼容入口。固定返回零，当前实现不会擦除或修改 Boot State A/B。
static inline uint8_t BootTest_CorruptBothStatePages(uint32_t image_size, uint32_t image_crc32, const char *image_version)
{
    (void)image_size; (void)image_crc32; (void)image_version; return 0U;
}
// 提供不启用 UFS 候选丢失注入的兼容查询。固定返回零，不删除模块文件或改变候选状态。
static inline uint8_t BootTest_UfsCandidateMissingRequested(uint32_t image_size, uint32_t image_crc32,
                                                             const char *image_version)
{
    (void)image_size; (void)image_crc32; (void)image_version; return 0U;
}
// 提供不启用 UFS 清理测试的兼容查询。固定返回零，当前实现不安排文件清理。
static inline uint8_t BootTest_UfsCleanupRequested(void) { return 0U; }
#endif

#endif
