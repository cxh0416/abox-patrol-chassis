#ifndef CENTER_CALIBRATION_H
#define CENTER_CALIBRATION_H
#include <stdint.h>

/* Session API; no HAL or RTOS types cross this boundary. */
uint8_t CenterCalibration_Start(void);
uint8_t CenterCalibration_IsActive(void);
uint8_t CenterCalibration_KeepAlive(const char* session_id);
uint8_t CenterCalibration_Jog(const char* session_id, int16_t delta_mdeg);
uint8_t CenterCalibration_Commit(const char* session_id);
uint8_t CenterCalibration_Cancel(const char* session_id);
uint8_t CenterCalibration_Format(char* out, uint16_t size);
uint8_t CenterCalibration_FormatDiagnostics(char* out, uint16_t size);
void CenterCalibration_Tick(uint32_t now);
#endif
