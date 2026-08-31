#ifndef PATROL_CHASSIS_OTA_INFO_H
#define PATROL_CHASSIS_OTA_INFO_H

#include <stdint.h>

#include "boot_cfg.h"

#define VCU_CONFIG_MAGIC 0x56435543U /* VCU C */
#define VCU_CONFIG_VERSION 3U

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    uint32_t checksum;
    uint8_t remote_enabled;
    uint8_t diag_log_enabled;
    uint8_t diag_log_level;
    uint8_t reserved0;
    char vid[32];
    char mqtt_host[64];
    uint16_t mqtt_port;
    uint16_t diag_log_port;
    char mqtt_username[32];
    char mqtt_password[64];
    char diag_log_host[64];
    char ota_origin[96];
    int32_t steering_center_count;
    uint32_t absolute_center_count;
    uint32_t absolute_counts_per_degree_milli;
    uint16_t steering_tolerance_mdeg;
    int8_t steering_direction;
    int8_t absolute_direction;
    uint8_t calibrated;
    uint8_t reserved1;
} VcuRemoteConfig_t;

typedef struct
{
    uint32_t ota_req_magic;
    uint32_t app_valid_magic;
    uint32_t fw_size;
    uint32_t fw_crc32;
    uint32_t req_fail_count;
    char url[128];
    VcuRemoteConfig_t remote_config;
    uint32_t reserved[8];
} OTA_Info_t;

void OTA_Info_Load(OTA_Info_t* info);
uint8_t OTA_Info_Save(const OTA_Info_t* info);
uint8_t OTA_Info_Clear(void);

uint8_t OTA_Info_LoadRemoteConfig(VcuRemoteConfig_t* cfg);
uint8_t OTA_Info_SaveRemoteConfig(const VcuRemoteConfig_t* cfg);

uint8_t OTA_Info_IsRequestValid(const OTA_Info_t* info);
uint8_t OTA_Info_HasRequest(void);
uint8_t OTA_Info_AppMarkedValid(void);
void OTA_Info_MarkAppValid(void);
void OTA_Info_ClearAppValid(void);
void OTA_Info_SetRequest(uint32_t fw_size, uint32_t fw_crc32, const char* url);
void OTA_Info_ClearRequest(void);
uint32_t OTA_Info_GetReqFailCount(void);
void OTA_Info_ClearReqFailCount(void);
void OTA_Info_IncReqFailCount(void);

#endif
