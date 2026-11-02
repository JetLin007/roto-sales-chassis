#include "stm32g4xx_hal.h"
#include "app_ota_download.h"
#include "port_spi_flash.h"
#include "ota_manager.h"
#include <string.h>
#include <stdio.h>

// 定义 4KB 的 RAM 缓存 (等同于 W25Q128 的一个扇区)
#define FLASH_SECTOR_SIZE 4096
static uint8_t g_ota_buffer[FLASH_SECTOR_SIZE];
static uint32_t g_buffer_fill = 0;       // 当前缓冲了多少字节
static uint32_t g_current_flash_addr = EXT_DOWNLOAD_BASE_ADDR; // 当前外部Flash的写入地址

void App_OTA_Download_Init(void) {
    g_buffer_fill = 0;
    g_current_flash_addr = EXT_DOWNLOAD_BASE_ADDR;
    printf("[OTA] Download init, ready to receive firmware...\r\n");
}

/* 内部函数：将缓冲区数据刷入外部 Flash */
static void flush_buffer_to_flash(void) {
    if (g_buffer_fill == 0) return;
    
    printf("[OTA] Flashing 4KB to ext addr: 0x%08lX\r\n", (unsigned long)g_current_flash_addr);
    
    // 1. 擦除目标扇区 (4KB)
    ExtFlash_Driver.erase_sector(g_current_flash_addr);
    
    // 2. 写入数据 (W25Q128 支持最多 256 字节/页，所以这里分页写)
    for (uint32_t i = 0; i < g_buffer_fill; i += 256) {
        uint32_t write_len = (g_buffer_fill - i) > 256 ? 256 : (g_buffer_fill - i);
        ExtFlash_Driver.write(g_current_flash_addr + i, &g_ota_buffer[i], write_len);
    }
    
    // 3. 更新地址和缓冲区状态
    g_current_flash_addr += g_buffer_fill;
    g_buffer_fill = 0;
}

void App_OTA_HandleRxData(uint32_t offset, uint8_t *data, uint32_t len) {
    // 安全检查：防止越界写入
    if (offset + len > EXT_DOWNLOAD_MAX_SIZE) {
        printf("[OTA] Error: Firmware size exceeds max limit!\r\n");
        return;
    }

    // 为了简化，假设 RS485 发来的包是按照顺序递增的，不存在丢包乱序
    // (实际工业通信中，可能需要判断 offset 是否等于当前已接收的总长度)
    
    uint32_t data_idx = 0;
    while (data_idx < len) {
        // 将数据填入 4KB 缓冲区
        uint32_t space_left = FLASH_SECTOR_SIZE - g_buffer_fill;
        uint32_t copy_len = (len - data_idx) > space_left ? space_left : (len - data_idx);
        
        memcpy(&g_ota_buffer[g_buffer_fill], &data[data_idx], copy_len);
        
        g_buffer_fill += copy_len;
        data_idx += copy_len;
        
        // 如果缓冲区满了，写入外部 Flash
        if (g_buffer_fill >= FLASH_SECTOR_SIZE) {
            flush_buffer_to_flash();
        }
    }
}

void App_OTA_FinishDownload(uint32_t total_size, uint32_t expected_crc) {
    // 1. 把剩下的尾巴（不足 4KB 的部分）也写入 Flash
    if (g_buffer_fill > 0) {
        flush_buffer_to_flash();
    }
    
    // 2. 更新 OTA 标志区
    OTA_Flag_t *flag = OTA_Get_Flag();
    flag->target_size = total_size;
    flag->target_crc = expected_crc;
    flag->current_state = OTA_STATE_DOWNLOAD_DONE;
    OTA_Update_Flag(flag);
    
    printf("[OTA] Download finished. Total: %lu bytes...\r\n", (unsigned long)total_size);
    
    // 3. 延时一会，确保串口输出完毕，然后软复位
    HAL_Delay(100);
    HAL_NVIC_SystemReset();
}