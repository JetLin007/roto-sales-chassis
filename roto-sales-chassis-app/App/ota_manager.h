#ifndef __OTA_MANAGER_H__
#define __OTA_MANAGER_H__

#include <stdint.h>
#include "ota_common.h"

/* 初始化并检查标志区 (上电时由 Bootloader 或 APP 调用) */
void OTA_Manager_Init(void);

/* 获取当前 OTA 状态 */
OTA_State_t OTA_Get_State(void);

/* 设置 OTA 状态并写入 Flash */
void OTA_Set_State(OTA_State_t state);

/* 获取整个标志区结构体指针 (方便其他模块读取 CRC/大小等) */
OTA_Flag_t* OTA_Get_Flag(void);

/* 写入完整的标志区信息 (更新状态、大小、CRC等) */
void OTA_Update_Flag(OTA_Flag_t *new_flag);

/* 清除标志区 (恢复出厂状态，用于 APP 启动确认后) */
void OTA_Clear_Flag(void);

/* 打印当前的 OTA 标志区信息 */
void OTA_PrintFlagInfo(void);

#endif /* __OTA_MANAGER_H__ */