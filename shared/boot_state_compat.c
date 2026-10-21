#include "boot_state.h"

#include <string.h>

#include "abox_boot_v2.h"

_Static_assert(sizeof(BootStateRecord_t) == sizeof(ABoxBootV2Record), "Boot State V3 ABI mismatch");
_Static_assert(sizeof(BootStateDiagnostics_t) == sizeof(ABoxBootV2Diagnostics), "Boot diagnostics ABI mismatch");

// 通过公共 Boot 算法计算给定字节区间的 CRC32。兼容接口不重新实现校验算法，结果必须与冻结 Boot 的记录解析保持一致。
uint32_t BootState_Crc32(const void *data, uint32_t length) { return ABoxBootV2_Crc32(data, length); }

// 通过公共存储加载有效且序号最新的 Boot State。空输出或没有有效记录时返回零，失败会清空输出，成功按已静态断言的 ABI 复制结构。
uint8_t BootState_Load(BootStateRecord_t *record)
{
    ABoxBootV2Record common;
    if (record == 0) return 0U;
    if (!ABoxBootV2_StateLoad(&common)) { memset(record, 0, sizeof(*record)); return 0U; }
    memcpy(record, &common, sizeof(*record));
    return 1U;
}

// 复制 Boot State A/B 页有效性与选择结果。即使没有有效选中页仍提供公共诊断内容，返回值表示是否存在有效记录。
uint8_t BootState_GetDiagnostics(BootStateDiagnostics_t *diagnostics)
{
    ABoxBootV2Diagnostics common;
    int valid;
    if (diagnostics == 0) return 0U;
    valid = ABoxBootV2_StateDiagnostics(&common);
    memcpy(diagnostics, &common, sizeof(*diagnostics));
    return (uint8_t)valid;
}

// 将兼容记录转换为公共 ABI 并保存到 A/B 状态页。序号递增、选页、CRC 和实际 Flash 写入由公共服务负责，调用者仍须遵守安全写入条件。
uint8_t BootState_Save(const BootStateRecord_t *record)
{
    ABoxBootV2Record common;
    if (record == 0) return 0U;
    memcpy(&common, record, sizeof(common));
    return (uint8_t)ABoxBootV2_StateSave(&common);
}

// 委托公共 Boot State 完成当前试运行确认。是否需要更新由公共状态决定，不能把一次接口返回当作整车运动或 OTA 现场验收。
uint8_t BootState_Confirm(void) { return (uint8_t)ABoxBootV2_Confirm(); }
// 确认回滚报告处理并更新公共 Boot 状态。只在符合公共回滚状态和稳定镜像条件时提交，不能伪造新候选已经成为稳定镜像。
uint8_t BootState_AcknowledgeRollback(void) { return (uint8_t)ABoxBootV2_AcknowledgeRollback(); }
