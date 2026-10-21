#ifndef PATROL_CAN_STATUS_H
#define PATROL_CAN_STATUS_H
#include <stdint.h>
// 读取 CAN 接收队列溢出累计次数。计数用于诊断丢帧风险，不代表驱动器已离线或已停止。
uint32_t PatrolRtos_CanRxOverflowCount(void);
// 读取指定 CAN 总线的原始接收帧计数。按总线编号区分来源，仅说明收到帧，不证明报文已通过协议和安全校验。
uint32_t PatrolRtos_CanRawRxCount(uint8_t bus);
// 查询产品 CAN 外设是否已完成启动。启动标志不等于当前无 Bus-Off，也不代表已收到节点反馈。
uint8_t PatrolRtos_CanStarted(void);
// 查询阻止运动的 CAN 总线故障门禁。结合当前实现维护的总线状态使用，恢复门禁仍须经过稳定反馈观察。
uint8_t PatrolRtos_CanBusOff(void);
// 查询 CAN 控制器当前是否仍处于活动 Bus-Off。与历史见过 Bus-Off 及恢复门禁分开，用于判断底层是否能继续发送停车报文。
uint8_t PatrolRtos_CanBusOffActive(void);
// 查询本轮是否记录过 Bus-Off 事件。历史标志用于诊断，不因控制器暂时恢复就丢弃故障证据。
uint8_t PatrolRtos_CanBusOffSeen(void);
// 用健康的一致反馈推进 Bus-Off 恢复观察。恢复要求故障之后的新样本、三次独立采样及至少一秒稳定区间；不会自动恢复旧运动会话。
void PatrolRtos_ObserveCanRecovery(uint32_t sample_ms, uint8_t healthy);
// 读取最近 Bus-Off 的单调毫秒时间戳。作为恢复样本必须晚于故障事件的基准，不是 UTC 时间。
uint32_t PatrolRtos_CanLastBusOffMs(void);
// 读取本轮 CAN 恢复已观察到的有效独立样本数。只有新鲜一致且满足安全条件的反馈才计数，不以轮询次数代替。
uint8_t PatrolRtos_CanRecoverySamples(void);
#endif
