# 编码器中位置位实测

用户明确授权当前位置作为机械中位；测试未发送转向点动。当前正式固件0.2.27。第一次抓包 rs485.jsonl 中途因旧监听恢复9600波特率而失真，不作为写应答依据。重新建立独占19200/8N1只读监听，rs485-valid.jsonl 捕获完整交互。

会话 c-38305-1：写请求 01 06 00 03 08 00 7E 0A；从站返回 01 86 01 83 A0；随后读回 01 03 04 0C C4 00 00 B8 9E。三帧 CRC 校验均通过。Modbus 异常码01为非法功能，证明该设备明确拒绝当前写请求，不能仅延长等待或忽略校验。不能单凭异常码推断所有06命令均不支持，或确定000F可用。

计数仍3268，标定未完成。commit d7196cf1-db1d-4a21-9d0d-d0c5d67f46fb；failure e545e6b3-93ab-4321-a0ed-07798f91704b；cancel e9abf1e7-15da-4018-9926-969c2f0bd942成功并关闭动力输出。需核对实际型号固件的置位功能与手册适用性。

原始来源 wyse3040 /home/cxh/patrol-evidence/commit485-2.jsonl 与 commit485-3.jsonl、server47只读SQLite。

rs485.jsonl SHA256 3a3946a19fb336d5deb214f7cb3972ef101ae191791fdcef3a71c57239dad297

rs485-valid.jsonl SHA256 eea77a30bc3310dcfaa1b68fdba757ba949343a54ce5d8c21effcbde8a9d2f9f

platform.json SHA256 d793ef02be3124eac5427b023880a9f4fcebe96d9296ee3773d7389683d22de8

