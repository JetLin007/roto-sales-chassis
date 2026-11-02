#include "app_modbus.h"
#include "main.h"
#include "app_ota_download.h"
#include <string.h>

extern UART_HandleTypeDef huart3;
extern DMA_HandleTypeDef hdma_usart3_rx;

// DMA 接收缓冲区
static uint8_t rx_dma_buf[MODBUS_RX_BUF_SIZE];
// 帧接收完成标志
static volatile uint8_t frame_received = 0;
static volatile uint16_t frame_length = 0;

/* 开启 DMA 接收 */
void App_Modbus_Init(void) {
    // 开启空闲中断
    __HAL_UART_ENABLE_IT(&huart3, UART_IT_IDLE);
    // 启动 DMA 接收
    HAL_UART_Receive_DMA(&huart3, rx_dma_buf, MODBUS_RX_BUF_SIZE);
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