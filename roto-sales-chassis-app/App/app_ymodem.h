#ifndef __APP_YMODEM_H__
#define __APP_YMODEM_H__

#include <stdint.h>

/* YModem 接收初始化 (准备接收) */
void App_YModem_Init(void);

/* 全局 OTA 模式标志: 0=Modbus, 1=YModem (app_modbus/app_freertos 共享) */
extern volatile uint8_t g_is_ota_mode;

/* 进入 OTA 模式后调用: 使能 YModem 的 DMA+IDLE 接收 */
void App_YModem_StartDMA(void);

/* 由 HAL_UARTEx_RxEventCallback 在 OTA 模式下调用 (ISR 上下文:仅拷贝整帧,不阻塞) */
void App_YModem_RxEvent(uint16_t Size);

/* 由 USART3 单字节接收中断调用，将字节放入 YModem 接收队列 */
void App_YModem_RxByte(uint8_t byte);

/* 任务上下文:处理整帧 YModem 协议,必要时发送 ACK/NAK */
void App_YModem_Process(void);

/* 获取 YModem 状态 (0:空闲/成功, 1:正在接收, 2:出错) */
uint8_t App_YModem_GetStatus(void);

#endif /* __APP_YMODEM_H__ */
