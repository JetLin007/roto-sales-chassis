#ifndef __APP_YMODEM_H__
#define __APP_YMODEM_H__

#include <stdint.h>

/* YModem 接收初始化 (准备接收) */
void App_YModem_Init(void);

/* 将 RS485 收到的字节喂给 YModem 状态机 */
void App_YModem_RxByte(uint8_t byte);

/* 获取 YModem 状态 (0:空闲/成功, 1:正在接收, 2:出错) */
uint8_t App_YModem_GetStatus(void);

#endif /* __APP_YMODEM_H__ */