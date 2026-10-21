#ifndef MEAL_TIME_SYNC_H
#define MEAL_TIME_SYNC_H
// 按设备身份初始化公共授时状态与请求响应 Topic。建立本轮请求种子和连接序号上下文，避免旧连接响应匹配新的授时请求。
void TimeSync_Init(const char *device_id);
// 在连接与发送条件允许时推进公共授时轮询。检测新的就绪周期并通过公共服务管理请求和超时，不阻塞等待网络时间。
void TimeSync_Task(void);
// 识别并校验本设备授时响应。验证 Topic、协议版本、requestId、状态码、服务器时间及误差，再交给公共同步服务关联本轮请求。
int TimeSync_OnMessage(const char *topic, const char *payload);
// 返回本设备时间同步响应 Topic。文本存储由模块持有，初始化或身份变化后内容可能更新，调用者不得修改或释放。
const char *TimeSync_ResponseTopic(void);
#endif
