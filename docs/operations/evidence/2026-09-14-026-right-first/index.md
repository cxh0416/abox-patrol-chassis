# 0.2.26 刷写与右转优先测试

候选 App 106848 字节，SHA256 A17D4914919ED127ABAA9007D06CC0793F1145EA4FC885C06D0C85FEB34D17D6。J-Link 读回验证，Boot、另一状态页、配置页不变；原状态有效记录序号 8，保留 stable 元数据后更新当前镜像记录为 NORMAL，由 App 完成本地 stable seed。平台 get_info 确认 0.2.26、CONFIRMED、IDLE、otaReady=true。私有完整 Flash 备份在 C:/Users/cxh/.codex/private/patrol-jlink-0.2.26-20260914，不入库。

会话 c-120961-1：先右 5 度，成功返回 READY，双路 4.926/4.923 度；随后左 5 度回到会话原点附近（0.075/0.090 度）时发生 benchFault=3、step22、gateMask=0。后续 1 度测试未执行。取消成功，动力输出关闭。

CAN 来源 wyse3040 /home/cxh/patrol-evidence/right-first026-xikMNP.log。右转停车：SDO 确认 140.160 ms，NMT 149.365 ms；左转停车：最后一次 SDO 在 180.371 ms 仍为 2237，TPDO 在 192.215 ms 变为 2231，但固件在下一次 SDO 前达到 200 ms 期限。第23步收尾修复已运行，第22步确认路径尚未通过。不得将本候选发布为正式 dist；现有 dist 保持 0.2.25。

can.log SHA256 31dce98f11afbf5ec25421d8d3e041375b2579ff15ef556464ed9401a1666f8a

platform.json SHA256 a1ebf256430db2affa6f5eb6cd4a791e7091c10d9f88e67d6cea27544ffc9b4c

backup.log SHA256 cf9bed7309b565fc8ac00b4f94caeb178bc40c2b23823a4d47a2acac67fc87f5

flash.log SHA256 b3e0adca52e80eece329bf2a6c19a05496a0f0f57ab082543839a3f67cd36890

