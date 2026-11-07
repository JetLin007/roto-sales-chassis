#include "stm32g4xx_hal.h"
#include "app_ota_download.h"
#include "port_spi_flash.h"
#include "ota_manager.h"
#include <string.h>
#include <stdio.h>

/* OTA RAM 缓冲区：整固件先放入 RAM，全部收完再统一写外部 Flash。
   这样避免每 4KB 擦写一次 Flash 阻塞 YModem 接收（半双工 RS485 下会导致超时丢包）。
   STM32G474 有 128KB RAM，64KB 缓冲足以容纳当前固件（~34KB）。 */
#define OTA_BUFFER_SIZE   (64 * 1024)
#define FLASH_SECTOR_SIZE 4096

static uint8_t g_ota_buffer[OTA_BUFFER_SIZE];
static uint32_t g_buffer_fill = 0;       // 当前缓冲了多少字节

void App_OTA_Download_Init(void) {
    g_buffer_fill = 0;
    printf("[OTA] Download init, ready to receive firmware...\r\n");
}

void App_OTA_HandleRxData(uint32_t offset, uint8_t *data, uint32_t len) {
    (void)offset;
    // 安全检查
    if (g_buffer_fill + len > OTA_BUFFER_SIZE) {
        printf("[OTA] Error: Firmware exceeds OTA RAM buffer (%u bytes)!\r\n",
               (unsigned)OTA_BUFFER_SIZE);
        return;
    }
    // 直接拷贝到 RAM 缓冲区，不写 Flash，避免阻塞 YModem 接收
    memcpy(&g_ota_buffer[g_buffer_fill], data, len);
    g_buffer_fill += len;
}

void App_OTA_FinishDownload(uint32_t total_size, uint32_t expected_crc) {
    /* 用实际文件大小写入 Flash（YModem 最后一包会用 0x1A/0x00 填充到 128 字节，
       但 bootloader 只需要 total_size 字节的有效固件）。
       如果 total_size 异常（为 0 或大于缓冲），则回退到 g_buffer_fill。 */
    uint32_t write_size = total_size;
    if (write_size == 0 || write_size > g_buffer_fill) {
        write_size = g_buffer_fill;
    }

    printf("[OTA] Writing %lu bytes (buf=%lu) to external Flash...\r\n",
           (unsigned long)write_size, (unsigned long)g_buffer_fill);

    // 1. 擦除所需扇区（每扇区 4KB，向上取整）
    uint32_t sectors = (write_size + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE;
    printf("[OTA] Erasing %lu sectors...\r\n", (unsigned long)sectors);
    for (uint32_t s = 0; s < sectors; s++) {
        printf("[OTA] Erase sector %lu (addr=0x%08lX)\r\n",
               (unsigned long)s, (unsigned long)(EXT_DOWNLOAD_BASE_ADDR + s * FLASH_SECTOR_SIZE));
        if (ExtFlash_Driver.erase_sector(EXT_DOWNLOAD_BASE_ADDR + s * FLASH_SECTOR_SIZE) != FLASH_OK) {
            printf("[OTA] ERROR: erase failed, abort download write.\r\n");
            return;
        }
    }
    printf("[OTA] Erase done. Writing data...\r\n");

    // 2. 分页写入（W25Q128 每页最多 256 字节）
    uint32_t addr = EXT_DOWNLOAD_BASE_ADDR;
    uint32_t remaining = write_size;
    uint8_t *src = g_ota_buffer;
    uint32_t page_count = 0;
    while (remaining > 0) {
        uint32_t chunk = remaining > 256 ? 256 : remaining;
        printf("[OTA] write page %lu: addr=0x%08lX chunk=%lu remaining=%lu\r\n",
               (unsigned long)page_count, (unsigned long)addr,
               (unsigned long)chunk, (unsigned long)remaining);
        if (ExtFlash_Driver.write(addr, src, chunk) != FLASH_OK) {
            printf("[OTA] ERROR: page write failed, abort download write.\r\n");
            return;
        }
        addr += chunk;
        src += chunk;
        remaining -= chunk;
        page_count++;
    }

    printf("[OTA] Flash write done.\r\n");

    // 3. 更新 OTA 标志区
    OTA_Flag_t *flag = OTA_Get_Flag();
    flag->target_size = total_size;
    flag->target_crc = expected_crc;
    flag->current_state = OTA_STATE_DOWNLOAD_DONE;
    OTA_Update_Flag(flag);

    printf("[OTA] Download finished. Total: %lu bytes...\r\n", (unsigned long)total_size);

    // 4. 延时一会，确保串口输出完毕，然后软复位
    HAL_Delay(100);
    HAL_NVIC_SystemReset();
}
