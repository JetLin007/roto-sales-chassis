#ifndef __OTA_COMMON_H__
#define __OTA_COMMON_H__

#include <stdint.h>
#include "ota_config.h"

/* --- 版本号字符串化处理 --- */
// 辅助宏
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

// 拼接成完整的版本字符串，例如 "v1.0.5"
#define FW_VERSION_STR  "v" TOSTRING(FW_VERSION_MAJOR) "." TOSTRING(FW_VERSION_MINOR) "." TOSTRING(FW_VERSION_PATCH)

/* ==================== 内部 Flash 规划 (512KB) ==================== */
#define BOOTLOADER_START_ADDR   0x08000000  // 32KB/64KB
#define OTA_FLAG_START_ADDR     0x08010000  // 标志区 (2KB)
#define APP_RUN_ADDR            0x08010800  // 运行区 (约 446KB)

/* ==================== 外部 SPI Flash 规划 (16MB) ==================== */
#define EXT_FLASH_SIZE          (16 * 1024 * 1024)
#define EXT_OTA_DOWNLOAD_ADDR   0x00000000  // 外部下载区起始 (1MB)
#define EXT_OTA_DOWNLOAD_MAX    (1 * 1024 * 1024)
#define EXT_OTA_BACKUP_ADDR     0x00100000  // 外部备份区起始 (1MB)
#define EXT_OTA_BACKUP_MAX      (1 * 1024 * 1024)

/* 魔术字 */
#define OTA_MAGIC_CODE          0x5A5A5A5A

typedef enum {
    OTA_STATE_IDLE = 0,         // 正常运行
    OTA_STATE_DOWNLOADING,      // 正在通过 RS485 往外部 Flash 下载新固件
    OTA_STATE_DOWNLOAD_DONE,    // 外部下载完成，准备重启进入 Bootloader 搬运
    OTA_STATE_TESTING,          // 新固件已搬运到内部 App，正在测试
    OTA_STATE_CONFIRMED,        // 新固件测试成功，正式运行
    OTA_STATE_ROLLBACK          // 新固件失败，正在从外部备份区回滚
} OTA_State_t;

typedef struct {
    uint32_t magic_code;        // 0x5A5A5A5A
    OTA_State_t current_state;  // 当前状态
    uint32_t target_size;       // 固件大小
    uint32_t target_crc;        // 固件 CRC32 校验值
    uint32_t boot_retry_cnt;    // 启动重试计数
    uint32_t version;           // 版本号
    uint8_t  reserved[8];       
} OTA_Flag_t;

#endif