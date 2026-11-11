#include <stdint.h>
void RemoteService_Revoke(void);
uint8_t RemoteService_HasCachedRequest(const char *);
int RemoteService_PowerOnPrepared(uint32_t,uint8_t *);
uint8_t RemoteService_HandleControl(const char *,uint16_t,char *,uint16_t);
