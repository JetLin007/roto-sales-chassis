#ifndef __APP_OTA_DOWNLOAD_H__
#define __APP_OTA_DOWNLOAD_H__

#include <stdint.h>
#include "ota_common.h"

/* 定义外部下载区在 W25Q128 中的起始地址 (假设我们用外部 Flash 的前 1MB) */
#define EXT_DOWNLOAD_BASE_ADDR   0x00000000 
#define EXT_DOWNLOAD_MAX_SIZE    (1 * 1024 * 1024) // 1MB 最大下载容量

/* 初始化下载任务 */
void App_OTA_Download_Init(void);

/* 处理上位机发来的固件数据包 (在 RS485 接收回调或任务中调用) */
/* offset: 当前包在固件中的偏移量, data: 数据指针, len: 数据长度 */
void App_OTA_HandleRxData(uint32_t offset, uint8_t *data, uint32_t len);

/* 完成下载，计算 CRC，更新标志并准备重启 */
void App_OTA_FinishDownload(uint32_t total_size, uint32_t expected_crc);

#endif /* __APP_OTA_DOWNLOAD_H__ */