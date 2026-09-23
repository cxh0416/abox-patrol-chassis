# 工作包 7.1 RTOS 启动异常 A/B 证据

## 边界

- 测试板、`abox-boot-2.3.0`、Config V5 和硬件环境保持不变，仅替换 App。
- Boot 未修改；设备此前运行送餐车固件，所用 Boot 与本产品相同。
- 正式 0.2.32 App 由发布清单对应源码重建，App bin SHA256 与 `dist` 完全一致：`5B9FE0E88D25ECFB34C278C6594974944257FA305CBD250F28539E048B2DFEE3`。
- 本记录是 RTOS/App 启动问题证据，不是 MQTT A/B/C/D1 验收证据。

## A：正式 0.2.32

断点命中顺序：`prvPortStartFirstTask -> SVC_Handler -> SysTick_Handler -> xPortSysTickHandler -> PendSV_Handler`。

| 项目 | 实测 |
|---|---:|
| VTOR | `0x08008000` |
| PRIMASK / BASEPRI / FAULTMASK | `0 / 0 / 0` |
| SysTick CTRL / LOAD / VAL | `0x7 / 0x1193F / 0x5353` |
| ICSR / SHPR3 | `0x00C29000 / 0xF0F00000` |
| SystemCoreClock | `72000000` |
| xTickCount | `0 -> 1 -> 502`，持续增长 |
| CanRx timeout | 100 ms 后返回，`R0=0` |
| watchdog health | `0x07` |

ELF/vector：PendSV symbol `0x0800C970`，vector `0x0800C971`；SysTick symbol `0x0800C6E8`，vector `0x0800C6E9`；SVC symbol `0x0800C8E0`。Thumb bit 均正确。

## B：原 7.1 增量 candidate

vector 和 RTOS handler 与正式 0.2.32 相同，五级断点均命中，`xTickCount 0 -> 1`。因此“SysTick 未接管”假设被排除。

随后进入 HardFault：stacked LR `0x0801B679`，stacked PC `0x0802318A`，CFSR `0x00000400`（BusFault IMPRECISERR），HFSR `0x40000000`。调用点位于 `ABoxBootV2App_Task()` 的 port callback；错误函数指针来自新旧 `ABoxBootV2AppPort` 布局混用。

## 根因与修复

Platform 新增了 `mqtt_client_index` port 回调。产品 OTA adapter 未同步赋值，且原 candidate 使用增量目录，旧 `ota_service.c.obj` 与新 Platform 对结构体布局的解释发生错配，最终把后续字段当成错误回调地址并 HardFault。

产品侧新增 `adapter_mqtt_client_index()`，返回 `MqttRuntime_ActiveClient()`，并赋给 `port.mqtt_client_index`；随后使用全新目录 clean build。Boot 未改，watchdog health 条件未放宽，也未伪造 CAN_RX health。

## 修复 candidate 实测

| 项目 | 实测 |
|---|---:|
| PendSV vector / ELF symbol | `0x0800C721 / 0x0800C720` |
| SysTick vector / ELF symbol | `0x0800C495 / 0x0800C494` |
| VTOR | `0x08008000` |
| PRIMASK / BASEPRI / FAULTMASK | `0 / 0 / 0` |
| SysTick CTRL / LOAD / VAL | `0x7 / 0x1193F / 0xF027`；独立复连采样 VAL `0xC6C4` |
| ICSR / SHPR3 | `0x00C29000 / 0xF0F00000` |
| SystemCoreClock | `72000000` |
| xTickCount | `0x299A -> 0x29CD`；脱离断点复连后 `0x5454A -> 0x55AC0` |
| CanRx timeout | 100 ms 后返回，`R0=0` |
| watchdog health | `0x07` |

clean Release RAM 为 61,432 / 65,536 B，guard 4,104 B。产品 36 项测试、Platform 14 项 CTest 和底盘 bench 测试全部通过。

结论：同一 Boot 下正式 App 和修复 candidate 的 RTOS tick、timeout 与 watchdog 均正常。原异常属于 App consumer/API 与陈旧增量对象混用；可以恢复工作包 7.1 MQTT/TLS 实机验收。
