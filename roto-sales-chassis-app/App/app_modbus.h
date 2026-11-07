#ifndef __APP_MODBUS_H__
#define __APP_MODBUS_H__

#include <stdint.h>

/* Modbus 从机地址 */
#define MODBUS_SLAVE_ADDR  0x01

/* 自定义的 OTA 功能码 (避开标准 Modbus 功能码) */
#define MODBUS_FC_OTA_START    0x41 // 开始升级
#define MODBUS_FC_OTA_DATA     0x42 // 传输数据
#define MODBUS_FC_OTA_END      0x43 // 结束升级
#define MODBUS_FC_OTA_STATUS   0x44 // 查询状态

/* 接收缓存 */
#define MODBUS_RX_BUF_SIZE 1024

/* 初始化并启动接收 */
void App_Modbus_Init(void);

/* 获取接收完成标志和长度 (供任务调用) */
uint8_t App_Modbus_GetFrame(uint8_t *buf, uint16_t *len);

/* 开启 Modbus 接收 */
void App_Modbus_Start(void);
/* 暂停 Modbus 接收 */
void App_Modbus_Stop(void);

#endif /* __APP_MODBUS_H__ */