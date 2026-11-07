#include "port_uart.h"
#include "main.h" // 包含 huart3 等底层句柄
#include "app_ymodem.h"
#include <stdio.h>

extern UART_HandleTypeDef huart3;

static UART_RxCallback_t g_rx_callback = NULL;
static uint8_t rx_byte;
static volatile uint8_t g_rs485_tx_active;
static volatile uint32_t g_rx_callback_count;
static volatile uint32_t g_rx_rearm_fail_count;
static volatile uint32_t g_last_uart_error;
static volatile uint32_t g_last_rx_rearm_status;
static volatile uint32_t g_rx_during_tx_count;

/* 初始化 RS485 */
void Port_UART_RS485_Init(void) {
    g_rx_callback_count = 0;
    g_rx_rearm_fail_count = 0;
    g_rx_during_tx_count = 0;
    g_last_uart_error = HAL_UART_ERROR_NONE;
    g_last_rx_rearm_status =
        (uint32_t)HAL_UART_Receive_IT(&huart3, &rx_byte, 1);
    if (g_last_rx_rearm_status != (uint32_t)HAL_OK) {
        g_rx_rearm_fail_count++;
    }
}

/* 停止单字节接收中断 (还给 Modbus) */
void Port_UART_RS485_DeInit(void) {
    HAL_UART_Abort_IT(&huart3); // 强行中断当前的 IT 接收
}

/* 发送单字节 (带 485 切换延时) */
void Port_UART_RS485_SendByte(uint8_t byte) {
    g_rs485_tx_active = 1;
    HAL_UART_Transmit(&huart3, &byte, 1, 100);
    
    // 🌟 极其关键：RS485 是半双工，发送完后必须加一点微小延时
    // 让 485 芯片有足够的时间把总线切换回“接收状态”
    // 否则上位机的 ACK 回应太快，会直接被吞掉！
    for (volatile int i = 0; i < 500; i++);
    g_rs485_tx_active = 0;
}

/* 发送数据块 */
void Port_UART_RS485_SendBuf(uint8_t *buf, uint16_t len) {
    g_rs485_tx_active = 1;
    HAL_UART_Transmit(&huart3, buf, len, 1000);
    for (volatile int i = 0; i < 500; i++);
    g_rs485_tx_active = 0;
}

/* 注册回调 */
void Port_UART_RegisterRxCallback(UART_RxCallback_t cb) {
    g_rx_callback = cb;
}

void Port_UART_GetRxDiagnostics(uint32_t *rx_count,
                                uint32_t *rearm_fail_count,
                                uint32_t *last_error,
                                uint32_t *last_rearm_status,
                                uint32_t *rx_during_tx_count) {
    if (rx_count != NULL) *rx_count = g_rx_callback_count;
    if (rearm_fail_count != NULL) *rearm_fail_count = g_rx_rearm_fail_count;
    if (last_error != NULL) *last_error = g_last_uart_error;
    if (last_rearm_status != NULL) *last_rearm_status = g_last_rx_rearm_status;
    if (rx_during_tx_count != NULL) *rx_during_tx_count = g_rx_during_tx_count;
}

/* 供 stm32g4xx_it.c 调用的接口 */
void Port_UART_RxHandler(uint8_t byte) {
    if (g_rx_callback != NULL) {
        g_rx_callback(byte);
    }
}

/* 下面这个回调放在这里，就不需要写在 stm32g4xx_it.c 里了 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART3) {
        // 先重新使能单字节接收，再分发业务，尽量压缩中断内处理导致的丢字节窗口
        g_rx_callback_count++;
        g_last_rx_rearm_status =
            (uint32_t)HAL_UART_Receive_IT(&huart3, &rx_byte, 1);
        if (g_last_rx_rearm_status != (uint32_t)HAL_OK) {
            g_rx_rearm_fail_count++;
        }
        if (g_is_ota_mode) {
            if (g_rs485_tx_active) {
                /* 发送期间收到的字节：正常半双工 485 会把自己的发送回显回来，
                   计数用于判断 RX 通道在发送时是否仍然有效。 */
                g_rx_during_tx_count++;
            } else {
                App_YModem_RxByte(rx_byte);
            }
        } else {
            Port_UART_RxHandler(rx_byte);
        }
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART3) {
        g_last_uart_error = huart->ErrorCode;
        if (g_is_ota_mode && huart->RxState == HAL_UART_STATE_READY) {
            g_last_rx_rearm_status =
                (uint32_t)HAL_UART_Receive_IT(&huart3, &rx_byte, 1);
            if (g_last_rx_rearm_status != (uint32_t)HAL_OK) {
                g_rx_rearm_fail_count++;
            }
        }
    }
}

// 在 port_uart.c 中添加
void Port_UART_Reset(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // 1. 彻底中止 DMA 接收，解除 HAL 库的 BUSY 锁定状态
    HAL_UART_DMAStop(&huart3);
    HAL_UART_Abort_IT(&huart3);

    // 2. 清除可能的过载/帧错误
    __HAL_UART_CLEAR_OREFLAG(&huart3);

    // 3. 重新执行底层 MSP 初始化 (重置引脚和底层时钟)
    HAL_UART_MspInit(&huart3);

    /* 关键：TP8485 自动方向电路靠 PB10(TX) 高电平使 Q3 导通进入接收态。
       MspInit 默认 NOPULL，若 USART 发送器在某个瞬间未驱动 TX，
       PB10 可能浮空导致 Q3 关断、接收器被禁用。加内部上拉作安全网。 */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitStruct.Pin = GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART3;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    // 4. 重新初始化串口 (它会把状态机重置为 READY)
    HAL_UART_Init(&huart3);
}

/* 读取 PB10(TX) 实际电平和 USART3 CR1，用于判断 485 方向电路是否处于接收态。
   PB10 应为高 (Q3 导通, RE/DE 低, 接收使能)。 */
void Port_UART_GetTxDiagnostics(uint8_t *pb10_level, uint32_t *cr1) {
    if (pb10_level != NULL) {
        *pb10_level = (uint8_t)HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_10);
    }
    if (cr1 != NULL) {
        *cr1 = huart3.Instance->CR1;
    }
}
