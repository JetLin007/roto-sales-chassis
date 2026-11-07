/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : app_freertos.c
  * Description        : Code for freertos applications
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

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "app_ota_download.h"
#include "app_ymodem.h"
#include "app_modbus.h"
#include "port_uart.h"
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
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
osThreadId defaultTaskHandle;

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void const * argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* definition and creation of defaultTask */
  osThreadDef(defaultTask, StartDefaultTask, osPriorityNormal, 0, 512);
  defaultTaskHandle = osThreadCreate(osThread(defaultTask), NULL);

  if (defaultTaskHandle == NULL) {
      printf("[FATAL] Task creation FAILED!\r\n");
  }

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void const * argument)
{
  /* USER CODE BEGIN StartDefaultTask */

  /* 1. 初始状态下，只开启 Modbus 的 DMA 接收，绝对不要开单字节中断 */
  App_Modbus_Start();
  
  printf("[APP] Modbus DMA started.\r\n");
  osDelay(1000); 
  printf("[APP] System Ready. Modbus RTU is running.\r\n");

  uint32_t ota_timeout_cnt = 0;  // YModem 超时计数器

  for(;;)
  {
    if (g_is_ota_mode == 0) 
    {
        /* ================== 默认状态：Modbus 模式 ================== */
        uint8_t modbus_rx_buf[256];
        uint16_t rx_len = 0;
        
        // 轮询 Modbus 数据
        if (App_Modbus_GetFrame(modbus_rx_buf, &rx_len)) 
        {
            printf("[DBG] Frame rx_len=%d, buf[0]=0x%02X, buf[1]=0x%02X\r\n",
                   rx_len, modbus_rx_buf[0], modbus_rx_buf[1]);
            if (rx_len >= 2) 
            {
                // 检查是否是 OTA 升级指令
                if (modbus_rx_buf[1] == 0x41) 
                {
                    printf("[APP] OTA command received, suspend Modbus, ready for YModem...\r\n");
                    
                    // 1. 彻底停止 Modbus 的 DMA 接收
                    App_Modbus_Stop();
                    
                    // 🌟 2. 强制复位 USART3 硬件，彻底清空错误标志，解决 DMA 和 IT 冲突
                    Port_UART_Reset();
                    
                    // 3. 切换为 OTA 模式 (让 IDLE 回调把整帧交给 YModem)
                    g_is_ota_mode = 1;
                    
                    // 4. 先准备接收，再发送 'C'，避免上位机首帧到达时漏收
                    App_OTA_Download_Init();
                    App_YModem_StartDMA();
                    App_YModem_Init();
                    
                    // 5. 复位超时计数
                    ota_timeout_cnt = 0;
                } 
                else 
                {
                    // 处理普通 Modbus 控制指令
                }
            }
        }
    } 
    else 
    {
        /* ================== 临时状态：YModem 模式 ================== */
        App_YModem_Process();   // 任务上下文处理 YModem 协议 (发送 ACK/NAK)
        ota_timeout_cnt++;
        if (ota_timeout_cnt > 3000) // 30秒超时
        {
            printf("[APP] YModem timeout! Restore Modbus.\r\n");
            
            // 1. 彻底停车 YModem 的 DMA 接收，防止与 Modbus 缓冲冲突
            Port_UART_RS485_DeInit();
            App_Modbus_Stop();
            g_is_ota_mode = 0;
            
            // 2. 重新开启 Modbus 的 DMA
            App_Modbus_Start();
        }
    }
    
    osDelay(10); // 让出 CPU
  }
  
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */
