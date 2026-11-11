#include <stdint.h>
#define CLOUD_LOG_SNAPSHOT_SIZE 976U
typedef struct { struct { uint8_t response_pending; } cloud; char log_snapshot[976]; char topic_control_resp[64]; char tx_payload[512]; uint8_t log_active; } CloudContext;
extern CloudContext g_cloud;
uint8_t CloudMessages_ScheduleRawResponse(const char *, const char *);
