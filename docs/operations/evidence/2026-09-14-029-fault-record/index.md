# 0.2.29 故障记录和复位验收

平台0.4.108已部署；App108584字节，SHA256 E81E0857431E66041EE643B71A375DCFC7ED0232E49A068235C262F1E7112C6F。J-Link读回一致且Boot/配置页/另一BootState页未变。

会话c-119376-1，仅开始并等待静止READY超时，没有点动或行走。135413ms产生来源2/原因1的快照；平台收到lastFault且保存状态1。复位8143542f-52d3-4ee4-bc56-3c9df8a3feae成功，输出false；许可和会话清空，最近记录不变。随后MCU重启，设备新状态报告仍带同一快照，get_info uptime31052ms、0.2.29 CONFIRMED/IDLE，输出false。

32项Python、8项CTest及产品C测试通过；Flash模拟覆盖中断写、满槽和配置重写保留，复位门禁与首次原因保留通过。现场未人为制造驱动器电气故障、Bus-Off或硬件过流；本次验收不能代替两路电机异常诊断。首次启动早期get_info超时，后续只读重试成功，未重放运动命令。

out/logs/wyse-029-backup.log → wyse-029-backup.log SHA256 84a55eae025f527350a26e793ff1fec75b32db78c212f33e2f9ddce3a11547e1

out/logs/wyse-029-flash.log → wyse-029-flash.log SHA256 4cb51b7b01ea660d8e111b7a1937894b496fba434dbdc13407dda74ed25fd925

out/logs/verify-fault029-result.log → verify-fault029-result.log SHA256 f4c4525bdc1a7dad01ab2a7eed7cad62bb914206d44636b1626de9ccb1b0fd2b

out/logs/reset029.log → reset029.log SHA256 06d4d55ff86514b65fb0e11e32cbc7b39203682a8750584bf767c3ca24d825b8

out/logs/fault029-after-reboot.log → fault029-after-reboot.log SHA256 b12abb90a1b4fa4ba012ae1cd6888cfeebd105d888d2b6cf6859e20a5e25b1dd

out/logs/final029-state.log → final029-state.log SHA256 993d1462d83083e7152a7b6313b94009553d67b0c0389e8d7d88519d7453284f

out/logs/tests029.log → tests029.log SHA256 6d4c77b4f5ccc92876d8b11dc0c08495566d5d7cf9245cd936c7f4ebd7bbc550

out/logs/platform108-tests.log → platform108-tests.log SHA256 f3b33eb53e61af43929bd223e03ebcdda8689696d247c1216978f1b986f78b1c

out/logs/platform108-deploy.log → platform108-deploy.log SHA256 1a568b858d7621edcd27b423b506e103e88b3cf69f6a037b9b1c4639bf186935
