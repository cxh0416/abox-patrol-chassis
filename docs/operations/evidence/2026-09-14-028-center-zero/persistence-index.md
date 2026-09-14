# 控制器重启后的补充核验

MCU 重启后固件 0.2.28，profileReady/rangeValid 为 true；驱动上电等待后 coordinateValid=true、feedbackGateMask=0、fault=0，最后已关闭电源输出。未做整车及编码器完全断电测试。get_calibration 被平台维护写策略拒绝，本次改用允许的 get_bench_steering 读取实际运行配置，不绕过平台策略。

原路径 out/logs/persistence028.log → persistence028.log；SHA256 10219ccd3a8eeb7722866574429c074d938b5a0a9aa5fb42489bd10ac90d2150

原路径 out/logs/persistence-powered028.log → persistence-powered028.log；SHA256 a9966ab7e5f87abaf69d11147c1a488f55f2c3652f232bb729f3bafb076adbab
