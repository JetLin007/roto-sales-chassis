#ifndef __PORT_FLASH_H__
#define __PORT_FLASH_H__

#include <stdint.h>

/* 定义 Flash 操作状态返回值 */
typedef enum {
    FLASH_OK = 0,
    FLASH_ERR_ERASE,
    FLASH_ERR_WRITE,
    FLASH_ERR_ALIGN,    // 地址未对齐 (G474 要求 8 字节/双字对齐)
    FLASH_ERR_RANGE     // 地址越界
} Flash_Status_t;

/* 定义跨平台函数指针结构体 */
typedef struct {
    Flash_Status_t (*init)(void);
    Flash_Status_t (*erase_sector)(uint32_t addr);
    Flash_Status_t (*write)(uint32_t addr, const uint8_t *data, uint32_t len);
    uint32_t       (*read)(uint32_t addr, uint8_t *data, uint32_t len);
} Flash_Driver_t;

/* 暴露给外部的全局句柄 */
extern const Flash_Driver_t Flash_Driver;

#endif /* __PORT_FLASH_H__ */