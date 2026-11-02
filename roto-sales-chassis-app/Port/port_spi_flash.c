#include "port_spi_flash.h"
#include "main.h" // 包含 HAL 库和引脚定义
#include <string.h>

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

/* 内部辅助函数：SPI 收发一个字节 */
static uint8_t spi_transfer(uint8_t tx_data) {
    uint8_t rx_data = 0;
    HAL_SPI_TransmitReceive(&hspi1, &tx_data, &rx_data, 1, 1000);
    return rx_data;
}

/* 内部辅助函数：等待 Flash 空闲 */
static void w25q_wait_busy(void) {
    W25Q_CS_LOW();
    spi_transfer(W25Q_CMD_READ_STATUS1);
    // 循环读取状态寄存器，直到 Busy 位(bit0)为 0
    while ((spi_transfer(0xFF) & 0x01) == 0x01); 
    W25Q_CS_HIGH();
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
    w25q_wait_busy();
    return FLASH_OK;
}

static Flash_Status_t w25q_write(uint32_t addr, const uint8_t *data, uint32_t len) {
    // W25Q128 最大单页写入为 256 字节，这里为了简单，假设外部传入的数据满足页对齐要求
    // (如果需要跨页写入，需要在这里做分页处理逻辑)
    
    // 1. 写使能
    W25Q_CS_LOW();
    spi_transfer(W25Q_CMD_WRITE_ENABLE);
    W25Q_CS_HIGH();
    
    // 2. 发送页写指令和 24 位地址
    W25Q_CS_LOW();
    spi_transfer(W25Q_CMD_PAGE_PROGRAM);
    spi_transfer((addr >> 16) & 0xFF);
    spi_transfer((addr >> 8) & 0xFF);
    spi_transfer(addr & 0xFF);
    
    // 3. 发送数据
    for (uint32_t i = 0; i < len; i++) {
        spi_transfer(data[i]);
    }
    W25Q_CS_HIGH();
    
    // 4. 等待写入完成
    w25q_wait_busy();
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