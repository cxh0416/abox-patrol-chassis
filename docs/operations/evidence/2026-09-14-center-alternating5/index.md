# 2026-09-14 左右 5 度测试

计划左 5 度后右 5 度；首个左转停车故障，右转未执行。session c-495548-4。取消成功并撤销动力输出。

CAN 来源：wyse3040 /home/cxh/patrol-evidence/left-right5-iiUHPR.log，只监听 500 kbit/s。

时序：6040=0006 于 1789366282.947693 发送；写应答 1789366282.948144；TPDO 状态 2231 于 1789366283.122426（174.733 ms）；SDO 6041=2231 于 1789366283.128204（180.511 ms）。失败 benchFault=3、benchStep=23、gateMask=0、steerSw=8753。固件已确认退使能并进入 STOP_COMPLETE，但在发 NMT pre-operational 收尾前触发总停车期限。CAN 中无该次停车的 NMT 80 01 收尾。不能将本次结论追溯为之前 step22 故障的精确时序。

两路相对角 -4.930/-4.886 度，差 0.044 度。没有修改固件或阈值。

can.log SHA256 db38f67645b3352cdee9b010f32819ea472af9f2b85c9d14adfdb5b8365328b7

platform.json SHA256 2ab53fe31b1bd7afbb8c7826a9a08084e12aeb76ca29abe2d728c9a244ab39d6

