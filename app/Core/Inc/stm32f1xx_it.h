/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32f1xx_it.h
  * @brief   This file contains the headers of the interrupt handlers.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __STM32F1xx_IT_H
#define __STM32F1xx_IT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
// 处理不可屏蔽异常并停留现场。当前实现永久等待，不执行自动恢复，不会返回到被中断的普通业务。
void NMI_Handler(void);
// 选择异常使用的 MSP 或 PSP 并转入 C 处理器。使用 naked 汇编入口避免编译器序言改写异常现场，依据 EXC_RETURN 的栈选择位传入原始堆栈。
void HardFault_Handler(void);
// 处理内存管理异常并永久等待。保留当前异常上下文，当前实现不清除异常或尝试继续运行业务。
void MemManage_Handler(void);
// 处理取指或数据访问总线异常并永久等待。当前实现保留现场，不把异常返回当成可恢复的普通外设错误。
void BusFault_Handler(void);
// 处理非法指令、状态或用法异常并永久等待。当前实现不自动修复寄存器或跳过故障指令。
void UsageFault_Handler(void);
// 提供 Cortex-M3 调试监控异常入口。当前函数体为空，由调试器和芯片调试配置决定是否使用。
void DebugMon_Handler(void);
// 处理 USART1 TX 所用 DMA1 通道 4 中断。转交对应 HAL DMA 句柄，发送完成与错误回调由 HAL 继续分发。
void DMA1_Channel4_IRQHandler(void);
// 处理 USART1 RX 所用 DMA1 通道 5 中断。转交对应 HAL DMA 句柄，接收数据归并在串口回调和云服务中处理。
void DMA1_Channel5_IRQHandler(void);
// 分发 CAN1 发送邮箱中断。交由 HAL_CAN_IRQHandler 处理状态与回调，中断入口不直接推进运动控制状态机。
void CAN1_TX_IRQHandler(void);
// 分发 CAN1 FIFO0 接收中断。由 HAL 触发接收回调后投递 RTOS 队列，业务解析在任务中完成。
void CAN1_RX0_IRQHandler(void);
// 分发 CAN1 FIFO1 接收中断。交由 HAL 处理对应实例，中断入口不自行解释驱动器协议。
void CAN1_RX1_IRQHandler(void);
// 分发 CAN1 状态变化和错误中断。由 HAL 更新错误并触发产品错误回调，Bus-Off 门禁在安全任务中持续监督。
void CAN1_SCE_IRQHandler(void);
// 分发 TIM1 更新中断。由 HAL_TIM_IRQHandler 触发时基回调，避免在中断入口重复增加 tick。
void TIM1_UP_IRQHandler(void);
// 分发 EC800 所用 USART1 中断。交由 HAL UART 处理空闲线和错误事件，实际环形接收与解析由后续服务完成。
void USART1_IRQHandler(void);
// 分发 UART5 外设中断。使用工程 UART5 句柄调用 HAL，中断入口不执行日志格式化或运动命令。
void UART5_IRQHandler(void);
// 分发 CAN2 发送邮箱中断。状态与回调由 HAL 处理，不在此确认远控命令业务成功。
void CAN2_TX_IRQHandler(void);
// 分发本地控制侧 CAN2 FIFO0 接收中断。接收回调将原始帧投递队列，T12 和本地协议校验在任务上下文执行。
void CAN2_RX0_IRQHandler(void);
// 分发 CAN2 FIFO1 接收中断。由 HAL 按实例维护接收状态，具体过滤与业务归属由上层配置决定。
void CAN2_RX1_IRQHandler(void);
// 分发 CAN2 状态变化和错误中断。通知 HAL 与产品错误回调，不能在此自动恢复旧运动授权。
void CAN2_SCE_IRQHandler(void);
/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

#ifdef __cplusplus
}
#endif

#endif /* __STM32F1xx_IT_H */
