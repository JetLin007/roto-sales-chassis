#include "app_modbus.h"
#include "main.h"
#include "app_ota_download.h"
#include "app_ymodem.h"
#include <string.h>
#include <stdio.h>

extern UART_HandleTypeDef huart3;
extern DMA_HandleTypeDef hdma_usart3_rx;

// DMA 接收缓冲区
static uint8_t rx_dma_buf[MODBUS_RX_BUF_SIZE];
// 帧接收完成标志
static volatile uint8_t frame_received = 0;
static volatile uint16_t frame_length = 0;

/* 开启 Modbus 接收 */
void App_Modbus_Start(void) {
    // 使用 ReceiveToIdle_DMA 使 HAL 内部 ReceptionType=TOIDLE，IDLE 中断才会触发回调
    HAL_UARTEx_ReceiveToIdle_DMA(&huart3, rx_dma_buf, MODBUS_RX_BUF_SIZE);
    // 必须先清除 pending 的 IDLE flag，再使能中断，否则 ISR 会立刻触发导致 HAL 状态机崩溃
    __HAL_UART_CLEAR_IDLEFLAG(&huart3);
    __HAL_UART_ENABLE_IT(&huart3, UART_IT_IDLE);
}

/* 暂停 Modbus 接收 (把串口让出来给 YModem) */
void App_Modbus_Stop(void) {
    __HAL_UART_DISABLE_IT(&huart3, UART_IT_IDLE);
    HAL_UART_DMAStop(&huart3);
}

/* 开启 DMA 接收 */
void App_Modbus_Init(void) {
    // 使用 ReceiveToIdle_DMA 使 HAL 内部 ReceptionType=TOIDLE
    HAL_UARTEx_ReceiveToIdle_DMA(&huart3, rx_dma_buf, MODBUS_RX_BUF_SIZE);
    __HAL_UART_ENABLE_IT(&huart3, UART_IT_IDLE);
}

/* 串口空闲中断回调 (需要在 stm32g4xx_it.c 或 main.c 的 HAL_UART_IdleCallback 中调用) */
void App_Modbus_IdleCallback(void) {
    // 清除空闲中断标志
    __HAL_UART_CLEAR_IDLEFLAG(&huart3);
    
    // 停止 DMA 以计算接收长度
    HAL_UART_DMAStop(&huart3);
    frame_length = MODBUS_RX_BUF_SIZE - __HAL_DMA_GET_COUNTER(&hdma_usart3_rx);
    
    if (frame_length > 0) {
        frame_received = 1;
    }
    
    // 重新启动 DMA
    HAL_UART_Receive_DMA(&huart3, rx_dma_buf, MODBUS_RX_BUF_SIZE);
}

/* 获取解析后的数据 */
uint8_t App_Modbus_GetFrame(uint8_t *buf, uint16_t *len) {
    if (frame_received) {
        // 这里可以加入 CRC16 校验逻辑 (Modbus RTU 标准)
        // 简单起见，我们直接将接收到的数据传出去
        memcpy(buf, rx_dma_buf, frame_length);
        *len = frame_length;
        frame_received = 0; // 清除标志
        return 1;
    }
    return 0;
}

/* ========== HAL 回调：IDLE 中断 → Modbus 帧接收 ========== */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance == USART3 && huart->RxEventType == HAL_UART_RXEVENT_IDLE)
    {
        /* OTA 模式下，DMA 缓冲已交还给 YModem，整帧交给它处理 */
        if (g_is_ota_mode)
        {
            App_YModem_RxEvent(Size);
            return;
        }

        if (Size > 0 && Size <= MODBUS_RX_BUF_SIZE)
        {
            printf("[DBG] IDLE RX: Size=%d, buf[0]=0x%02X, buf[1]=0x%02X\r\n",
                   Size, rx_dma_buf[0], rx_dma_buf[1]);
            frame_length = Size;
            frame_received = 1;
        }
        // HAL 已停止 DMA、清除 IDLE 标志，这里只需重新使能 IDLE 并重启 DMA
        __HAL_UART_CLEAR_IDLEFLAG(&huart3);
        __HAL_UART_ENABLE_IT(&huart3, UART_IT_IDLE);
        HAL_UARTEx_ReceiveToIdle_DMA(&huart3, rx_dma_buf, MODBUS_RX_BUF_SIZE);
    }
}