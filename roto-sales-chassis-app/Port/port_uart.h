#ifndef __PORT_UART_H__
#define __PORT_UART_H__

#include <stdint.h>

/* 初始化 RS485 调试串口 (底层初始化，供 APP/Boot 调用) */
void Port_UART_RS485_Init(void);

/* 停止单字节接收中断 (还给 Modbus) */
void Port_UART_RS485_DeInit(void);

/* 发送单字节 (自带 485 半双工切换延时) */
void Port_UART_RS485_SendByte(uint8_t byte);

/* 发送数据块 */
void Port_UART_RS485_SendBuf(uint8_t *buf, uint16_t len);

/* 接收中断回调注册 (把底层的字节传给 YModem 或 Modbus) */
typedef void (*UART_RxCallback_t)(uint8_t byte);
void Port_UART_RegisterRxCallback(UART_RxCallback_t cb);

/* 获取 USART3 接收中断诊断信息 */
void Port_UART_GetRxDiagnostics(uint32_t *rx_count,
                                uint32_t *rearm_fail_count,
                                uint32_t *last_error,
                                uint32_t *last_rearm_status,
                                uint32_t *rx_during_tx_count);

void Port_UART_Reset(void);

/* 获取 PB10(TX) 电平和 USART3 CR1，用于判断 485 方向电路状态 */
void Port_UART_GetTxDiagnostics(uint8_t *pb10_level, uint32_t *cr1);

#endif /* __PORT_UART_H__ */
