# Quectel 厂商参考资料

## LTE Standard(U) FILE 应用指导 V1.6

- 归档文件：`Quectel_LTE_StandardU_FILE_Application_Guide_V1.6_CN.pdf`
- 原始文件名：`Quectel_LTE_StandardU系列_FILE_应用指导_V1.6.pdf`
- 文档日期：2024-06-20
- SHA-256：`D9CAAF3F9FC825F8EA8154C6A78829A559B6B93C2D2E2DD19FAAF85A9CC87173`
- 来源：用户提供的 Quectel 文档，仅保留作工程参考。

与 OTA 相关的要求：

- 第 1.2 节要求所有由 `AT+QFOPEN` 返回的句柄最终都通过 `AT+QFCLOSE` 关闭。
- 第 2.3.6 节把模式 `0` 定义为“不存在则创建”，模式 `2` 定义为“仅打开已有文件并只读”。
- 第 2.3.4、2.3.7 节规定 `QFUPL`、`QFREAD` 的 `CONNECT` 数据模式行为。
- 第 4 章把 `402` 定义为读取零字节、`405` 定义为文件不存在、`426` 定义为文件已打开。

适用性表明确包含 EC800G-CN，但不代表所有以 EC800 命名的产品都完全相同。把命令细节作为正式模块依据前，仍需使用 `ATI`、`AT+QGMR` 确认实际模块型号和固件版本。
