#include "ota_manager.h"
#include "port_flash.h"
#include <string.h>
#include <stdio.h>

/* 在 RAM 中缓存标志区的数据 */
static OTA_Flag_t g_ota_flag_cache;
static uint8_t g_ota_flag_initialized = 0;

void OTA_Manager_Init(void) {
    Flash_Driver.init();

    // 1. 从 Flash 读取当前标志区
    Flash_Driver.read(OTA_FLAG_START_ADDR, (uint8_t*)&g_ota_flag_cache, sizeof(OTA_Flag_t));
    
    // 2. 校验魔术字是否合法
    if (g_ota_flag_cache.magic_code != OTA_MAGIC_CODE) {
        // 如果不合法（首次运行或Flash被擦除），初始化默认状态
        memset(&g_ota_flag_cache, 0, sizeof(OTA_Flag_t));
        g_ota_flag_cache.magic_code = OTA_MAGIC_CODE;
        g_ota_flag_cache.current_state = OTA_STATE_IDLE;
        
        // 写入默认值到 Flash
        Flash_Driver.erase_sector(OTA_FLAG_START_ADDR);
        
        // G474 内部 Flash 必须按 8 字节对齐写入，补齐结构体长度
        uint8_t temp_buf[64] = {0};
        memcpy(temp_buf, &g_ota_flag_cache, sizeof(OTA_Flag_t));
        Flash_Driver.write(OTA_FLAG_START_ADDR, temp_buf, 64);
    }
    
    g_ota_flag_initialized = 1;
}

OTA_State_t OTA_Get_State(void) {
    return g_ota_flag_cache.current_state;
}

OTA_Flag_t* OTA_Get_Flag(void) {
    return &g_ota_flag_cache;
}

void OTA_Update_Flag(OTA_Flag_t *new_flag) {
    if (new_flag == NULL) return;

    Flash_Driver.init();
    
    // 1. 更新 RAM 缓存
    memcpy(&g_ota_flag_cache, new_flag, sizeof(OTA_Flag_t));
    g_ota_flag_cache.magic_code = OTA_MAGIC_CODE;
    
    // 2. 擦除标志区扇区
    if (Flash_Driver.erase_sector(OTA_FLAG_START_ADDR) != FLASH_OK) {
        printf("[OTA] ERROR: erase OTA flag failed!\r\n");
        return;
    }
    
    // 3. 写入新数据 (G474 要求 8 字节对齐)
    uint8_t temp_buf[64] = {0};
    memcpy(temp_buf, &g_ota_flag_cache, sizeof(OTA_Flag_t));
    if (Flash_Driver.write(OTA_FLAG_START_ADDR, temp_buf, 64) != FLASH_OK) {
        printf("[OTA] ERROR: write OTA flag failed!\r\n");
        return;
    }

    OTA_Flag_t verify;
    Flash_Driver.read(OTA_FLAG_START_ADDR, (uint8_t*)&verify, sizeof(OTA_Flag_t));
    printf("[OTA] Flag written: state=%lu size=%lu crc=0x%08lX\r\n",
           (unsigned long)verify.current_state,
           (unsigned long)verify.target_size,
           (unsigned long)verify.target_crc);
}

void OTA_Set_State(OTA_State_t state) {
    OTA_Flag_t temp = g_ota_flag_cache;
    temp.current_state = state;
    OTA_Update_Flag(&temp);
}

void OTA_Clear_Flag(void) {
    OTA_Flag_t temp;
    memset(&temp, 0, sizeof(OTA_Flag_t));
    temp.magic_code = OTA_MAGIC_CODE;
    temp.current_state = OTA_STATE_IDLE;
    OTA_Update_Flag(&temp);
}

#include "ota_manager.h"
#include <stdio.h>

/* 将 OTA 状态枚举转为可读的字符串 */
const char* OTA_GetStateString(OTA_State_t state) {
    switch (state) {
        case OTA_STATE_IDLE:            return "IDLE ";
        case OTA_STATE_DOWNLOADING:     return "DOWNLOADING ";
        case OTA_STATE_DOWNLOAD_DONE:   return "DOWNLOAD_DONE ";
        case OTA_STATE_TESTING:         return "TESTING ";
        case OTA_STATE_CONFIRMED:       return "CONFIRMED ";
        case OTA_STATE_ROLLBACK:        return "ROLLBACK ";
        default:                        return "UNKNOWN";
    }
}

/* 打印当前的 OTA 标志区信息 */
void OTA_PrintFlagInfo(void) {
    OTA_Flag_t *flag = OTA_Get_Flag();
    
    printf("\r\n--- OTA Flag Info ---\r\n");
    printf("Magic Code : 0x%08lX\r\n", (unsigned long)flag->magic_code);
    printf("State      : %s\r\n", OTA_GetStateString(flag->current_state));
    printf("Target Size: %lu Bytes\r\n", (unsigned long)flag->target_size);
    printf("Target CRC : 0x%08lX\r\n", (unsigned long)flag->target_crc);
    printf("Retry Cnt  : %lu / 3\r\n", (unsigned long)flag->boot_retry_cnt);
    printf("Version    : %lu\r\n", (unsigned long)flag->version);
    printf("---------------------\r\n\r\n");
}
