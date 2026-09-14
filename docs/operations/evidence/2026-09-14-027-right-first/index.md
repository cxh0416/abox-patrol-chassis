# 0.2.27 右转优先验收

App SHA256 85B8777D2B84BA9ABF4D64B250EE5D7F1971D498CB782552B4F668D53085F337，107088 字节。J-Link 备份并读回验证 App；Boot、另一状态页、配置页保持原样。完整备份位于私有目录 C:/Users/cxh/.codex/private/patrol-jlink-0.2.27-20260914。平台确认 0.2.27、CONFIRMED、IDLE、otaReady。

会话 c-72493-1，右5、左5、右5、左5、右1、左1，共六次点动均返回 phase=1/fault=0，测试结束取消并下电。此验证覆盖点动停车与继续标定，不是完整中位写入/掉电保存验收。

CAN 原始来源 wyse3040 /home/cxh/patrol-evidence/right-first027-9Y8WhD.log。每次退使能主动状态/收尾 NMT 相对停车写命令的毫秒数：[{"pdo_ms": 188.857, "nmt_ms": 189.429}, {"pdo_ms": 117.384, "nmt_ms": 119.754}, {"pdo_ms": 186.262, "nmt_ms": 190.56}, {"pdo_ms": 127.225, "nmt_ms": 129.864}, {"pdo_ms": 36.028, "nmt_ms": 39.79}, {"pdo_ms": 104.408, "nmt_ms": 109.83}]。使能前实时读取 1800:01 与 1A00:01 确认映射。

can.log SHA256 8e867538f9552b450575321acb0efa9ae7dbc7011cf6d9bc0cd88f286405cb08

platform.json SHA256 75b38c8a6ce5699f130ff5a17e5723fc39eedc10de832f90344acbdbaee3d898

backup.log SHA256 7f2a38f5f294817c904a3a4f20c012e5e9acd117c6ab29a2247fd410eda68837

flash.log SHA256 56c0b771c55ab9a2794e9abf577d3c59f8c1b3477f94f6e71b30b68528678114

