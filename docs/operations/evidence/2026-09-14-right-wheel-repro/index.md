# 右轮异常低速复测

0.2.28，用户授权直接测试。J-Link重启后，平台正式调度器会话分别发10/30 rpm前进约3秒，前后保持零速。使用真实设备颁发许可，固件与调度安全门禁保留。

会话 b-1789370896416-1、b-1789370984793-2。CAN显示60FF=+10/+30，68FF=-10/-30，写入均有回显。运动窗口6069持续0，6869出现负向速度，30 rpm测试约-31至-34。6041=0037/1037，无fault bit；2000/2001为0。未复现此前00BF故障，不能认定硬件根因或现场左右映射。

第二次power_off响应超时（db8820c5-272a-4348-9145-a37e2e2e41b9），但随后平台最新状态timestamp1789371028747确认powerOutputOn=false、两轮0、motionState=stopped、faultLatched=false。不是仅凭发命令判断停机。

out/logs/right-wheel-repro-complete.log → right-wheel-repro-complete.log SHA256 b8992b8af15be645174de6df8061c8f8b9b961becf922e1d1ceb72ceb618990e

out/logs/repro-drive-result.log → repro-drive-result.log SHA256 6f12bd9e0045c4ba85542f964d241817175d0cc5cd38ed79827700fae9334052

out/logs/repro-drive30-result.log → repro-drive30-result.log SHA256 d346816e215cd5517f70100d574020f98f2da0d330788049ed9c5c13d9bd2902

out/logs/repro_drive.py → repro_drive.py SHA256 847689e39dc35e265627f839d4f4cf9382c24b851571f645c92a9501d1d31b86
