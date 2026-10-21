#ifndef ABOX_BOOT_STATE_H
#define ABOX_BOOT_STATE_H

#include <stdint.h>

#include "boot_cfg.h"

#define BOOT_STATE_MAGIC 0x42535632U /* BSV2 */
#define BOOT_STATE_VERSION 3U

#define BOOT_SLOT_NONE 0xFFU
#define BOOT_ERROR_NONE 0U
#define BOOT_ERROR_CANDIDATE_INVALID 1U
#define BOOT_ERROR_INSTALL_IO 2U
#define BOOT_ERROR_INSTALL_CRC 3U
#define BOOT_ERROR_TRIAL_FAULT 4U
#define BOOT_ERROR_ROLLED_BACK 5U

typedef enum {
    BOOT_STATE_NORMAL = 0,
    BOOT_STATE_INSTALL_PENDING,
    BOOT_STATE_INSTALLING,
    BOOT_STATE_TRIAL,
    BOOT_STATE_CONFIRMED,
    BOOT_STATE_ROLLBACK,
} BootStateCode_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    uint32_t sequence;
    uint32_t state;
    uint8_t stable_slot;
    uint8_t candidate_slot;
    uint8_t trial_fail_count;
    uint8_t last_reset_reason;
    uint32_t image_size;
    uint32_t image_crc32;
    char image_version[32];
    uint32_t stable_image_size;
    uint32_t stable_image_crc32;
    char stable_image_version[32];
    uint32_t last_error;
    uint8_t rollback_report_pending;
    uint8_t rollback_installed;
    uint8_t reserved[2];
    uint32_t record_crc32;
} BootStateRecord_t;

typedef struct {
    uint8_t page_a_valid;
    uint8_t page_b_valid;
    uint8_t selected_page;
    uint8_t selected_valid;
    uint32_t page_a_sequence;
    uint32_t page_b_sequence;
    uint32_t selected_sequence;
} BootStateDiagnostics_t;

// 通过公共 Boot 算法计算给定字节区间的 CRC32。兼容接口不重新实现校验算法，结果必须与冻结 Boot 的记录解析保持一致。
uint32_t BootState_Crc32(const void *data, uint32_t length);
// 通过公共存储加载有效且序号最新的 Boot State。空输出或没有有效记录时返回零，失败会清空输出，成功按已静态断言的 ABI 复制结构。
uint8_t BootState_Load(BootStateRecord_t *record);
// 复制 Boot State A/B 页有效性与选择结果。即使没有有效选中页仍提供公共诊断内容，返回值表示是否存在有效记录。
uint8_t BootState_GetDiagnostics(BootStateDiagnostics_t *diagnostics);
// 将兼容记录转换为公共 ABI 并保存到 A/B 状态页。序号递增、选页、CRC 和实际 Flash 写入由公共服务负责，调用者仍须遵守安全写入条件。
uint8_t BootState_Save(const BootStateRecord_t *record);
// 委托公共 Boot State 完成当前试运行确认。是否需要更新由公共状态决定，不能把一次接口返回当作整车运动或 OTA 现场验收。
uint8_t BootState_Confirm(void);
// 确认回滚报告处理并更新公共 Boot 状态。只在符合公共回滚状态和稳定镜像条件时提交，不能伪造新候选已经成为稳定镜像。
uint8_t BootState_AcknowledgeRollback(void);

#endif
