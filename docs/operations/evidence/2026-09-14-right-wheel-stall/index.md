# 右轮不转与远控退出：故障后只读核验

现场用户报告前进时右后轮不转。会话 b-1789370234614-1 在 15:17:34 gate_lost 结束。故障后诊断保留左轮37 rpm、右轮0；当前反馈已过期，不能解释为仍在运动。Node2 6041=191 (0x00BF)，bit3置位；2000/2001最后采样均0，轮询周期不同，不能认定没有发生驱动故障。bench driveFault=3表示执行器安全门禁失败，并非厂商故障码3。

中位和单圈范围有效，当前电源输出false，故障锁存。故障时段没有wyse原始CAN抓包，未证明右轮目标实际写入/回显，也未定位机械或电气原因。本轮仅诊断查询，无上电、使能或运动指令。

out/logs/rear-wheel-platform.log → rear-wheel-platform.log SHA256 750a32b6e30fb3e97eb4303ee5526ce4034ab55810d278e78f048ae0ca2bcaaa

out/logs/rear-wheel-current.log → rear-wheel-current.log SHA256 a25a7f9f54ab4b99e433d4902dc052273013979e94608556bf33f9b4ea3fdc7b

out/logs/rear-wheel-audit.log → rear-wheel-audit.log SHA256 016b44df5fdbb7f7e4a639bc8b503884c0e90da866ef7c05206dee176b8328e3

out/logs/rear-wheel-traces.jsonl → rear-wheel-traces.jsonl SHA256 0d6fbf73cc842b240d2e0c148d66196f579ee5d30339a875cff833bc424aeefd

out/logs/rear-wheel-session.log → rear-wheel-session.log SHA256 70cb2b3b51defb7b17396fa43c83a8ef7187022988476b11545129e1aaeee737
