#ifndef __PORT_SPI_FLASH_H__
#define __PORT_SPI_FLASH_H__

#include <stdint.h>
#include "port_flash.h" // 借用 Flash_Status_t 枚举

#define W25Q128_SECTOR_SIZE     4096
#define W25Q128_PAGE_SIZE       256

typedef struct {
    Flash_Status_t (*init)(void);
    Flash_Status_t (*erase_sector)(uint32_t addr);
    Flash_Status_t (*write)(uint32_t addr, const uint8_t *data, uint32_t len);
    uint32_t       (*read)(uint32_t addr, uint8_t *data, uint32_t len);
} ExtFlash_Driver_t;

extern const ExtFlash_Driver_t ExtFlash_Driver;

#endif /* __PORT_SPI_FLASH_H__ */