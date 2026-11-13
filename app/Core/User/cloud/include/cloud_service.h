#ifndef PATROL_CHASSIS_CLOUD_SERVICE_H
#define PATROL_CHASSIS_CLOUD_SERVICE_H

#include <stdint.h>


// 初始化 EC800、MQTT、注册、OTA 与云调度适配器。绑定固定接收存储和平台服务，加载本车配置并建立安全默认状态；关键初始化失败时保持通信服务未就绪。
void CloudService_Init(void);
// 判断当前配置是否仍需首次身份与凭据注册。考虑试连中的保护阶段及默认 VID 标记，避免在已进行的维护流程中重复注册。
uint8_t CloudService_NeedsEnrollment(void);
// 轮询公共 App 服务及可选工位注册流程。仅在初始化成功后执行，由云任务持有服务上下文；每轮处理有限状态，不在运动任务内阻塞等待网络。
void CloudService_Task(void);
// 清除网络变更前的云发送在途关联。通知公共云服务，重置外部消息类型及候选取消标记，避免旧回执完成新连接的动作。
void CloudTx_NetworkChanged(void);
// 应用 VID 对应的 Client ID、Topic 与时间同步订阅。先验证 VID 并逐项构造公共和内部 Topic，再更新订阅集合；此操作不直接重建控制会话。
uint8_t CloudService_ApplyMqttIdentity(const char* vid);

#endif
