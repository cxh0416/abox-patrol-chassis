#include "abox_product_config.h"
/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : PatrolVehicleChassis VCU safe bare-metal baseline.
 ******************************************************************************
 */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"
#include "can.h"
#include "dma.h"
#include "iwdg.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>

#include "app_log.h"
#include "abox_stm32f105_ec800_port.h"
#include "app_version.h"
#include "boot_cfg.h"
#include "cloud_service.h"
#include "remote_service.h"
#include "time_base.h"
#include "patrol_config.h"
#include "vehicle_power_port.h"
#include "abox_uart.h"
#include "abox_board.h"
#include "abox_watchdog_port.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
// 配置本工程系统时钟、总线分频及 Flash 等待周期。按当前板级 HSE/PLL 配置执行，任何 HAL 配置失败均进入 Error_Handler，不能随意沿用其他硬件时钟参数。
void SystemClock_Config(void);
// 创建本产品 FreeRTOS 运行对象。转交 PatrolRtos_Init 创建静态任务和队列，随后由主函数启动内核调度。
void MX_FREERTOS_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
// 执行 App 启动初始化并启动 RTOS 调度器。先设置 App 向量表并核验 HAL 时基，再初始化外设、配置、电源端口和服务；调度启动前不自动授予运动许可。
int main(void)
{

  /* USER CODE BEGIN 1 */
  SCB->VTOR = APP_START_ADDR;
  __DSB();
  __ISB();

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  if (HAL_Init() != HAL_OK) Error_Handler();

  /* USER CODE BEGIN Init */
  /* Boot disables and clears NVIC before handing off with PRIMASK set.
   * VTOR now selects App handlers; HAL_Init installs the TIM1 tick. Validate
   * that tick before enabling interrupts needed by clock-init timeouts. */
  if (SCB->VTOR != APP_START_ADDR ||
      (TIM1->CR1 & TIM_CR1_CEN) == 0U ||
      (TIM1->DIER & TIM_DIER_UIE) == 0U ||
      NVIC_GetEnableIRQ(TIM1_UP_IRQn) == 0U) Error_Handler();
  __enable_irq();

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  if (!ABoxBoardUart_Prepare(ABOX_BOARD_UART_MODEM) ||
      !ABoxBoardUart_Prepare(ABOX_BOARD_UART_BUS)) Error_Handler();
  {
    static const ABoxUartConfig bus = {ABOX_PRODUCT_BUS_UART_BAUD, 8U, 1U, ABOX_UART_PARITY_NONE, ABOX_UART_FLOW_NONE, .mode=ABOX_UART_ASYNC};
    static const ABoxUartConfig modem = {ABOX_PRODUCT_MODEM_UART_BAUD, 8U, 1U, ABOX_UART_PARITY_NONE, ABOX_UART_FLOW_NONE, .mode=ABOX_UART_ASYNC};
    if (!ABoxUart_Open(ABOX_BOARD_UART_BUS, &bus) || !ABoxUart_Open(ABOX_BOARD_UART_MODEM, &modem)) Error_Handler();
  }
  if (!ABoxWatchdog_Init(64U, 1250U)) Error_Handler();
  /* USER CODE BEGIN 2 */
  ABoxStm32F105Ec800Port_Init();
  AppLog_Init();
  AppLog_FieldStartup(Patrol_AppFwVersion());
  TimeBase_Init();
  PatrolConfig_Load();
  VehiclePowerPort_Init();
  CloudService_Init();
  RemoteService_Init();
  /* Before the first valid board sample, collision inputs cannot grant motion. */
  RemoteService_CollisionInputs(0U, 0U, HAL_GetTick());
  APP_LOGI("BOOT", "version=%s app=0x%08lX", Patrol_AppFwVersion(),
           (unsigned long)APP_START_ADDR);

  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();  /* Call init function for freertos objects (in cmsis_os2.c) */
  MX_FREERTOS_Init();

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
// 配置本工程系统时钟、总线分频及 Flash 等待周期。按当前板级 HSE/PLL 配置执行，任何 HAL 配置失败均进入 Error_Handler，不能随意沿用其他硬件时钟参数。
void SystemClock_Config(void)
{
  static const ABoxMcuClockConfig clock = ABOX_PRODUCT_CLOCK_CONFIG;
  if (ABoxMcuClock_Apply(&clock) != ABOX_HAL_OK) Error_Handler();
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM1 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
// 在 TIM1 更新事件中维护 HAL 毫秒 tick。仅对 TIM1 调用 HAL_IncTick，避免把其他定时器事件重复计入系统时间。
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM1)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
// 在无法继续的初始化或 HAL 错误后终止执行。关闭中断并永久等待，当前实现不返回，也不保证外部驱动器已经收到停车命令。
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
// 提供启用完整 HAL 断言时的错误报告钩子。参数是触发文件和行号，当前函数体预留为空，不自动打印或复位。
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */



//ljk