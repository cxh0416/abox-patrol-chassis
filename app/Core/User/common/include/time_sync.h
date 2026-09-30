#ifndef MEAL_TIME_SYNC_H
#define MEAL_TIME_SYNC_H
void TimeSync_Init(const char *device_id);
void TimeSync_Task(void);
int TimeSync_OnMessage(const char *topic, const char *payload);
const char *TimeSync_ResponseTopic(void);
#endif
