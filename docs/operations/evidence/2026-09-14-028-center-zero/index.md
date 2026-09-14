# 0.2.28 中位提交与跨零测试

用户此前确认当前机械位置可设为中位。0.2.28 App 107176 字节，SHA256 1F1B6BEAC34D658D10E22B5990B94B39786D6B96E86436413755A10266151339，J-Link读回一致并保留Boot/另一状态页/配置页。

会话c-129285-1：固件发送01060003000F39CE，编码器正常回显；随后连续读回0，phase7、calibrated/profileReady/rangeValid均true，驱动器中位-83815。取消后标定仍有效，动力关闭。

会话c-159093-2：右1、左1、左1、右1，四次均READY且fault0；正侧49计数约+0.886度，负侧4044计数约-0.941度。末次回到原点附近4091计数约-0.090度，没有覆盖之前确认的中位。

本记录覆盖固件置零回显、配置保存和单圈跨零；不代表整车/编码器完全断电保持性已验证。串口来源wyse /home/cxh/patrol-evidence/commit485-028.jsonl，平台来源server47只读SQLite。

rs485.jsonl SHA256 2a76a14cf676d9521fe2aa60eb2aa708a050e802c5a6f2c86c82658d35568167

platform.json SHA256 852d58e9a64d3eff886b4081ec0c86c7d974a9888fa31f0b6cb5c8c9d5cc6ccb

backup.log SHA256 ed640be3124f2759424fba3213a95daf028364531c78e906dbbe24950d2dc15d

flash.log SHA256 ac50dd0ba9d138cb6a8c4db1d380ebe6e2ee9efa8b986469411b0f72498c2125

