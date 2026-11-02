#ifndef __OTA_CONFIG_H__
#define __OTA_CONFIG_H__

/* ==================== 内部 Flash 规划 (STM32G474 512KB) ==================== */
/* 采用单区运行模式：Bootloader + 标志区 + 唯一的 App 运行区 */
#define BOOTLOADER_START_ADDR   0x08000000
#define BOOTLOADER_MAX_SIZE     (64 * 1024)     // 64 KB (给Bootloader留足空间)

#define OTA_FLAG_START_ADDR     0x08010000      // 标志区 (独立 2KB 扇区)
#define OTA_FLAG_SIZE           (2 * 1024)

#define APP_RUN_ADDR            0x08010800      // App 运行区起始
#define APP_RUN_MAX_SIZE        (446 * 1024)    // App 运行区最大 446KB (内部其余全给App)


/* ==================== 外部 SPI Flash 规划 (W25Q128 16MB) ==================== */
/* 外部 Flash 地址为芯片内的偏移地址 (0x00000000 ~ 0x00FFFFFF) */
#define EXT_FLASH_TOTAL_SIZE    (16 * 1024 * 1024)

/* 下载区：存放 RK3576 下发的新固件 (预留 1MB 空间) */
#define EXT_OTA_DOWNLOAD_ADDR   0x00000000
#define EXT_OTA_DOWNLOAD_MAX    (1 * 1024 * 1024)

/* 备份区：存放升级前最后一次运行正常的旧固件 (预留 1MB 空间) */
#define EXT_OTA_BACKUP_ADDR     0x00100000
#define EXT_OTA_BACKUP_MAX      (1 * 1024 * 1024)

/* 剩下的 14MB 外部空间可以自由支配 (字库、日志等) */


/* ==================== 编译目标相关的宏定义 ==================== */
#if defined(TARGET_BOOTLOADER)
    /* Bootloader 工程专用配置 */
    #define ACTIVE_FLASH_BASE       BOOTLOADER_START_ADDR
    #define ACTIVE_FLASH_SIZE       BOOTLOADER_MAX_SIZE

#elif defined(TARGET_APP)
    /* APP 工程专用配置 */
    #define ACTIVE_FLASH_BASE       APP_RUN_ADDR
    #define ACTIVE_FLASH_SIZE       APP_RUN_MAX_SIZE
    
    /* APP 的中断向量表偏移量 (VTOR) */
    #define APP_VTOR_OFFSET         (APP_RUN_ADDR - BOOTLOADER_START_ADDR)

#else
    #error "Please define TARGET_BOOTLOADER or TARGET_APP in your compiler preprocessor settings!"
#endif

#endif /* __OTA_CONFIG_H__ */