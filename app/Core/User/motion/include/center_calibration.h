#ifndef CENTER_CALIBRATION_H
#define CENTER_CALIBRATION_H
#include <stdint.h>

typedef struct {
    uint8_t phase;
    uint8_t reason;
    uint8_t active;
    uint8_t calibrated;
    int32_t driver_raw;
    uint32_t encoder_raw;
    int32_t jog_offset_mdeg;
    int32_t saved_center_count;
} CenterCalibrationStatus;

/* Session API; no HAL or RTOS types cross this boundary. */
// 在全部前置门禁通过后建立新的中位标定会话。拒绝已有活动标定或执行器占用，记录起始双传感器坐标和会话期限；此时尚未完成永久标定。
uint8_t CenterCalibration_Start(void);
// 判断中位标定是否仍占用控制流程。按当前标定阶段判断，不以是否已经发送过点动命令推断活动状态。
uint8_t CenterCalibration_IsActive(void);
// 续租当前有效的标定会话。仅对匹配会话和允许阶段生效，不能以保活绕过实际反馈门禁或恢复已终止流程。
uint8_t CenterCalibration_KeepAlive(const char* session_id);
// 申请一次受限的标定相对点动。核验会话、阶段、安全门禁和增量幅度，再更新本次相对目标；驱动器与编码器反馈随后独立验证位移。
uint8_t CenterCalibration_Jog(const char* session_id, int16_t delta_mdeg);
// 申请把已确认的机械中位保存为正式标定。要求匹配会话、执行器停止及撤使能等前提，协调编码器写入复读和 Config V5 保存，不在请求接受时宣称已落盘。
uint8_t CenterCalibration_Commit(const char* session_id);
// 取消指定会话的标定操作。按当前阶段执行安全退出或有序停车；旧会话不能取消新会话占用。
uint8_t CenterCalibration_Cancel(const char* session_id);
// 输出当前标定业务状态与会话信息。供响应构造使用；返回格式化结果，不代表标定提交或真实转向已通过验收。
uint8_t CenterCalibration_Format(char* out, uint16_t size);
// 输出标定内部诊断及最近失败证据。使用有界缓冲区给出门禁、阶段、相对角度和错误，不改变标定状态。
uint8_t CenterCalibration_FormatDiagnostics(char* out, uint16_t size);
// 返回当前会话标识的只读指针。调用方必须在运行临界区内立即使用，不能跨任务保存或释放。
const char* CenterCalibration_SessionId(void);
// 取得只读标定快照。数值来自同一运行上下文，仅用于状态显示和本地维护响应，不构成标定完成或许可证明。
uint8_t CenterCalibration_GetStatus(CenterCalibrationStatus* status);
// 推进标定超时、停车、编码器校验及保存阶段。持续监督门禁与双反馈一致性，仅在所需确认全部成立后推进提交，失败走统一证据捕获与安全退出。
void CenterCalibration_Tick(uint32_t now);
#endif
