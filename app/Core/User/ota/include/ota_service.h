#ifndef PATROL_CHASSIS_OTA_SERVICE_H
#define PATROL_CHASSIS_OTA_SERVICE_H

#include <stdint.h>
#include "cloud_protocol.h"
#include "abox_ec800_at_legacy.h"

#define OTA_ERROR_EC800_UNSUPPORTED 1001U
#define OTA_ERROR_CA_PROVISION_FAILED 1002U
#define OTA_ERROR_URL_REJECTED 1101U
#define OTA_ERROR_HTTP_FAILED 1201U
#define OTA_ERROR_FILE_SIZE_MISMATCH 1301U
#define OTA_ERROR_FILE_CRC_MISMATCH 1302U
#define OTA_ERROR_IMAGE_VECTOR_INVALID 1303U
#define OTA_ERROR_STATE_WRITE_FAILED 1401U
#define OTA_ERROR_UFS_IO_FAILED 1501U

// 将产品布局、版本、CA 和共享 EC800 服务绑定到公共 OTA。先使旧初始化就绪状态失效，再绑定冻结分区和当前镜像尺寸；绑定失败时保持 OTA 不可用。
void OtaService_Init(Ec800AtCore_t* at);
// 查询公共 OTA App 服务是否完成初始化。仅说明服务已绑定，下载条件、预置和忙状态仍需分别检查。
uint8_t OtaService_IsInitialized(void);
// 请求开始 OTA/TLS 所需模块文件预置。异步请求由公共服务推进，返回接受结果不代表文件已写好或校验完成。
uint8_t OtaService_BeginProvisioning(void);
// 推进公共 OTA App 服务一次。在统一云任务与 AT 资源仲裁下调用，产品不复制公共下载或安装状态机。
void OtaService_Task(void);
// 查询 OTA 所需模块文件是否已经预置完成。供 TLS 和注册前置门禁使用，不表示存在可安装固件。
uint8_t OtaService_IsProvisioned(void);
// 查询 OTA 服务是否具备接收新请求的就绪条件。不能仅凭初始化成功或网络连接成功替代此判断。
uint8_t OtaService_IsReady(void);
// 查询 OTA 是否占用升级或预置流程。该结果参与运动禁止与共享 AT/Flash 互斥，忙时不能绕过执行运动。
uint8_t OtaService_IsBusy(void);
// 取得公共 OTA 当前阶段名称。返回服务持有的只读文本，用于内部诊断，不作为现场升级验收结论。
const char* OtaService_Phase(void);
// 读取公共 OTA 保存的最近错误码。零值或其他编号含义遵循公共 OTA 服务定义，不自行清除原始错误。
uint32_t OtaService_LastError(void);
// 把已解析的产品 OTA 请求转为公共升级请求。核对初始化、指针及 URL/版本容量，再传递大小与 CRC；接受请求不代表下载、烧写或启动成功。
uint8_t OtaService_Start(const CloudProtocolRequest_t* request);
// 消费一次安装准备完成事件。非零表示该事件可由业务层安排 INSTALL_PENDING 响应和后续重启，不能重复消费为多次成功。
uint8_t OtaService_TakeInstallReady(void);
// 消费一次 OTA 失败事件并取得错误码。仅在存在新失败时写出 error 并返回非零，供响应调度使用。
uint8_t OtaService_TakeFailure(uint32_t* error);
// 请求公共 Boot State 确认回滚报告已处理。修改状态仍受公共存储校验约束；本接口不把回滚后的固件重新标记为新候选已验收。
void OtaService_AcknowledgeRollback(void);
// 返回当前 OTA 目标版本文本。指针由公共服务持有，跨任务或后续请求需复制，不得修改或释放。
const char* OtaService_TargetVersion(void);

#endif
