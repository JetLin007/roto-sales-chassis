#include "port_flash.h"
#include "stm32g4xx_hal.h"
#include "ota_config.h"

static Flash_Status_t stm32_flash_init(void) {
    // 解锁 Flash
    HAL_FLASH_Unlock();
    return FLASH_OK;
}

static Flash_Status_t stm32_flash_erase_sector(uint32_t addr) {
    FLASH_EraseInitTypeDef EraseInitStruct;
    uint32_t PageError = 0;

    // 检查是否 2KB 对齐 (G474 Page size = 2KB)
    if (addr % 2048 != 0) return FLASH_ERR_ALIGN;

    // 1. 关全局中断 (极其重要，防止擦除期间被中断打断导致总线卡死)
    __disable_irq();

    // 结合正点原子手册 433 页提示：擦除期间必须关闭数据缓存
    FLASH->ACR &= ~(1 << 10); 

    EraseInitStruct.TypeErase = FLASH_TYPEERASE_PAGES;
    EraseInitStruct.Banks = FLASH_BANK_1; 
    EraseInitStruct.Page = (addr - 0x08000000) / 2048;
    EraseInitStruct.NbPages = 1;

    if (HAL_FLASHEx_Erase(&EraseInitStruct, &PageError) != HAL_OK) {
        FLASH->ACR |= (1 << 10); // 恢复缓存
        __enable_irq(); // 恢复中断
        return FLASH_ERR_ERASE;
    }

    FLASH->ACR |= (1 << 10); // 恢复缓存
    __enable_irq(); // 恢复中断
    return FLASH_OK;
}

static Flash_Status_t stm32_flash_write(uint32_t addr, const uint8_t *data, uint32_t len) {
    // G474 要求按双字(64-bit, 8字节) 编程写入
    if (addr % 8 != 0 || len % 8 != 0) return FLASH_ERR_ALIGN;

    for (uint32_t i = 0; i < len; i += 8) {
        uint64_t double_word = *(uint64_t *)(data + i);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr + i, double_word) != HAL_OK) {
            return FLASH_ERR_WRITE;
        }
    }
    return FLASH_OK;
}

static uint32_t stm32_flash_read(uint32_t addr, uint8_t *data, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        data[i] = *(volatile uint8_t *)(addr + i);
    }
    return len;
}

/* 实例化结构体供外部调用 */
const Flash_Driver_t Flash_Driver = {
    .init = stm32_flash_init,
    .erase_sector = stm32_flash_erase_sector,
    .write = stm32_flash_write,
    .read = stm32_flash_read
};