# 左右 1 度交替现场验证

固件 0.2.25，session c-292014-3。左右各 1 度交替，共四次，均反馈 phase=1、fault=0。未修改固件；未证明原 5 度问题消失。

CAN 原始来源 /home/cxh/patrol-evidence/20260914-center-stop-can.log，此文件为本次下载时的快照。

停车阶段四次 6040=0006 至 SDO 确认退使能分别约 160、120、120、140 ms。准备阶段另有四次相同控制字写入（约 20 ms 确认），不计作停车。当前 200 ms 期限还包含命令发送前调度时间和确认后的收尾时间。

can.log SHA256 b4799b4e15d7db842431b718e11fd35a218dfe309c17fa14dcdd7ac4754dd32b

platform.json SHA256 549f8a0a7b59cb67bf2af94c89e0b084357143f7470888d62956d24cdf152f0a

