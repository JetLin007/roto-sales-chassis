#include "port_spi_flash.h"
#include "main.h" // 包含 HAL 库和引脚定义
#include <string.h>
#include <stdio.h>

// 假设 CubeMX 配置的 SPI 句柄叫 hspi1
extern SPI_HandleTypeDef hspi1;

/* W25Q128 常用指令 */
#define W25Q_CMD_WRITE_ENABLE   0x06
#define W25Q_CMD_READ_STATUS1   0x05
#define W25Q_CMD_READ_DATA      0x03
#define W25Q_CMD_PAGE_PROGRAM   0x02
#define W25Q_CMD_SECTOR_ERASE   0x20
#define W25Q_CMD_JEDEC_ID       0x9F

/* 片选控制宏定义 (根据你的 CubeMX 配置修改引脚) */
#define W25Q_CS_LOW()   HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET)
#define W25Q_CS_HIGH()  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET)

/* W25Q 全双工收发：SPI 读状态/读数据都依赖发送 dummy byte 产生时钟。 */
static uint8_t spi_transfer(uint8_t tx_data) {
    uint8_t rx_data = 0xFF;
    HAL_StatusTypeDef st = HAL_SPI_TransmitReceive(&hspi1, &tx_data, &rx_data, 1, 100);
    if (st != HAL_OK) {
        printf("[FLASH] ERROR: SPI xfer failed st=%d err=0x%08lX SR=0x%08lX\r\n",
               (int)st, (unsigned long)HAL_SPI_GetError(&hspi1),
               (unsigned long)hspi1.Instance->SR);
        return 0xFF;
    }
    return rx_data;
}

/* 内部辅助函数：等待 Flash 空闲 (墙钟超时 5 秒) */
static int w25q_wait_busy(void) {
    W25Q_CS_LOW();
    spi_transfer(W25Q_CMD_READ_STATUS1);

    uint32_t start_tick = HAL_GetTick();
    uint8_t status = 0xFF;
    uint32_t print_cnt = 0;
    while (1) {
        status = spi_transfer(0xFF);
        if ((status & 0x01) == 0) break;      /* Busy 位清零，编程完成 */
        if ((HAL_GetTick() - start_tick) >= 5000) {
            W25Q_CS_HIGH();
            printf("[FLASH] ERROR: wait_busy TIMEOUT (5s) status=0x%02X\r\n", status);
            return -1;
        }
        if ((print_cnt++ % 200) == 0) {
            printf("[FLASH] busy... status=0x%02X elapsed=%lums\r\n",
                   status, (unsigned long)(HAL_GetTick() - start_tick));
        }
        HAL_Delay(1);
    }
    W25Q_CS_HIGH();
    return 0;
}

static Flash_Status_t w25q_init(void) {
    W25Q_CS_HIGH();
    // 读取 ID 验证通信是否正常
    W25Q_CS_LOW();
    spi_transfer(W25Q_CMD_JEDEC_ID);
    uint8_t manuf_id = spi_transfer(0xFF);
    uint8_t mem_type = spi_transfer(0xFF);
    uint8_t capacity = spi_transfer(0xFF);
    W25Q_CS_HIGH();
    
    // W25Q128 的 JEDEC ID 通常是 0xEF4018
    if (manuf_id == 0xEF && mem_type == 0x40 && capacity == 0x18) {
        return FLASH_OK;
    }
    return FLASH_ERR_WRITE; // 返回错误表示没识别到芯片
}

static Flash_Status_t w25q_erase_sector(uint32_t addr) {
    if (addr % W25Q128_SECTOR_SIZE != 0) return FLASH_ERR_ALIGN;
    
    printf("[FLASH] erase_sector start: 0x%08lX\r\n", (unsigned long)addr);
    
    // 1. 写使能
    W25Q_CS_LOW();
    spi_transfer(W25Q_CMD_WRITE_ENABLE);
    W25Q_CS_HIGH();
    
    // 2. 发送擦除指令和 24 位地址
    W25Q_CS_LOW();
    spi_transfer(W25Q_CMD_SECTOR_ERASE);
    spi_transfer((addr >> 16) & 0xFF);
    spi_transfer((addr >> 8) & 0xFF);
    spi_transfer(addr & 0xFF);
    W25Q_CS_HIGH();
    
    // 3. 等待擦除完成
    if (w25q_wait_busy() != 0) {
        printf("[FLASH] erase_sector FAILED: 0x%08lX\r\n", (unsigned long)addr);
        return FLASH_ERR_WRITE;
    }
    printf("[FLASH] erase_sector done: 0x%08lX\r\n", (unsigned long)addr);
    return FLASH_OK;
}

static Flash_Status_t w25q_write(uint32_t addr, const uint8_t *data, uint32_t len) {
    printf("[FLASH] write start: addr=0x%08lX len=%lu data[0..3]=%02X %02X %02X %02X\r\n",
           (unsigned long)addr, (unsigned long)len,
           data[0], data[1], data[2], data[3]);

    // 1. 写使能
    W25Q_CS_LOW();
    spi_transfer(W25Q_CMD_WRITE_ENABLE);
    W25Q_CS_HIGH();
    printf("[FLASH] write WREN done\r\n");

    // 2. 发送页写指令和 24 位地址
    W25Q_CS_LOW();
    spi_transfer(W25Q_CMD_PAGE_PROGRAM);
    spi_transfer((addr >> 16) & 0xFF);
    spi_transfer((addr >> 8) & 0xFF);
    spi_transfer(addr & 0xFF);
    printf("[FLASH] write cmd sent, sending %lu data bytes...\r\n",
           (unsigned long)len);

    // 3. 发送数据
    for (uint32_t i = 0; i < len; i++) {
        spi_transfer(data[i]);
    }
    W25Q_CS_HIGH();
    printf("[FLASH] write data sent, waiting busy...\r\n");

    // 4. 等待写入完成
    if (w25q_wait_busy() != 0) {
        printf("[FLASH] write FAILED: 0x%08lX len=%lu\r\n", (unsigned long)addr, (unsigned long)len);
        return FLASH_ERR_WRITE;
    }
    printf("[FLASH] write done: 0x%08lX\r\n", (unsigned long)addr);
    return FLASH_OK;
}

static uint32_t w25q_read(uint32_t addr, uint8_t *data, uint32_t len) {
    W25Q_CS_LOW();
    spi_transfer(W25Q_CMD_READ_DATA);
    spi_transfer((addr >> 16) & 0xFF);
    spi_transfer((addr >> 8) & 0xFF);
    spi_transfer(addr & 0xFF);
    
    for (uint32_t i = 0; i < len; i++) {
        data[i] = spi_transfer(0xFF);
    }
    W25Q_CS_HIGH();
    
    return len;
}

/* 实例化函数指针结构体 */
const ExtFlash_Driver_t ExtFlash_Driver = {
    .init = w25q_init,
    .erase_sector = w25q_erase_sector,
    .write = w25q_write,
    .read = w25q_read
};
