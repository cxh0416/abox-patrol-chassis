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

void OtaService_Init(Ec800AtCore_t* at);
uint8_t OtaService_IsInitialized(void);
uint8_t OtaService_BeginProvisioning(void);
void OtaService_Task(void);
uint8_t OtaService_IsProvisioned(void);
uint8_t OtaService_IsReady(void);
uint8_t OtaService_IsBusy(void);
const char* OtaService_Phase(void);
uint32_t OtaService_LastError(void);
uint8_t OtaService_Start(const CloudProtocolRequest_t* request);
uint8_t OtaService_TakeInstallReady(void);
uint8_t OtaService_TakeFailure(uint32_t* error);
void OtaService_AcknowledgeRollback(void);
const char* OtaService_TargetVersion(void);

#endif
