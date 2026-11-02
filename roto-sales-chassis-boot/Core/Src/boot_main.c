#include "ota_manager.h"  // 加上这个，解决 OTA_Get_Flag 等警告
#include "ota_config.h"
#include "main.h"
#include "ota_common.h"
#include "port_flash.h"
#include "port_spi_flash.h"
#include <stdio.h>
#include <string.h>

// 声明外部 SPI Flash 的驱动句柄
extern const ExtFlash_Driver_t ExtFlash_Driver;

// 声明内部 Flash 的驱动句柄
extern const Flash_Driver_t Flash_Driver;

/* 跳转函数的类型定义 */
typedef void (*pFunction)(void);
pFunction JumpToApplication;

/* 外部 Flash 搬运到内部 Flash 的缓冲区 (1KB) */
#define COPY_BUFFER_SIZE 1024
static uint8_t copy_buffer[COPY_BUFFER_SIZE];

/* 
 * 核心功能：搬运固件并跳转
 * 从外部下载区读取，覆盖内部运行区
 */
void Boot_UpdateAndJump(void) {
    OTA_Flag_t *flag = OTA_Get_Flag();
    uint32_t total_size = flag->target_size;
    uint32_t total_crc = flag->target_crc;
    
    printf("Starting firmware update process...\r\n");
    printf("Target Size: %lu bytes, CRC: 0x%08lX\r\n", (unsigned long)total_size, (unsigned long)total_crc);

    // 1. 擦除内部 App 运行区
    printf("Erasing internal App region...\r\n");
    // 根据 APP_RUN_MAX_SIZE 计算需要擦除多少个 2KB 扇区
    for (uint32_t addr = APP_RUN_ADDR; addr < (APP_RUN_ADDR + total_size); addr += 2048) {
        if (Flash_Driver.erase_sector(addr) != FLASH_OK) {
            printf("Erase internal flash failed!\r\n");
            return; // 擦除失败，直接死机，等待重新上电
        }
    }

    // 2. 从外部 SPI Flash 搬运数据到内部 Flash
    printf("Copying from external Flash to internal Flash...\r\n");
    uint32_t copied_size = 0;
    while (copied_size < total_size) {
        uint32_t chunk_size = (total_size - copied_size) > COPY_BUFFER_SIZE ? COPY_BUFFER_SIZE : (total_size - copied_size);
        
        // 从外部读取
        ExtFlash_Driver.read(EXT_OTA_DOWNLOAD_ADDR + copied_size, copy_buffer, chunk_size);
        
        // 写入内部 (注意：G474 内部 Flash 必须 8 字节对齐写入)
        // 为了安全，补齐为 8 的倍数
        uint32_t write_len = (chunk_size + 7) & ~7; 
        if (Flash_Driver.write(APP_RUN_ADDR + copied_size, copy_buffer, write_len) != FLASH_OK) {
            printf("Write internal flash failed!\r\n");
            return;
        }
        
        copied_size += chunk_size;
    }
    
    printf("Firmware copied successfully!\r\n");
    
    // 3. 更新标志区：进入测试状态，并清零重试次数
    flag->current_state = OTA_STATE_TESTING;
    flag->boot_retry_cnt = 0;
    OTA_Update_Flag(flag);
    
    printf("Update done. Rebooting to test new App...\r\n");
    HAL_Delay(100);
    NVIC_SystemReset();
}

/*
 * 核心功能：检查并跳转到 APP
 */
void Boot_JumpToApp(void) {
    // 检查 APP 运行区的栈顶地址是否合法 (必须在 SRAM 范围内: 0x20000000)
    uint32_t app_stack_pointer = *(volatile uint32_t*)APP_RUN_ADDR;
    
    if ((app_stack_pointer & 0x2FFE0000) == 0x20000000) {
        // 合法 App
        printf("Jumping to App at 0x%08X...\r\n", APP_RUN_ADDR);
        HAL_Delay(50); // 稍作延时，让串口打印完

        // 1. 关闭全局中断
        __disable_irq();
        
        // 2. 关闭 SysTick 定时器
        SysTick->CTRL = 0;
        SysTick->LOAD = 0;
        SysTick->VAL = 0;
        
        // 3. 将 APP 的栈顶地址写入 MSP
        __set_MSP(app_stack_pointer);
        
        // 4. 获取 APP 的复位中断函数地址
        uint32_t app_jump_addr = *(volatile uint32_t*)(APP_RUN_ADDR + 4);
        JumpToApplication = (pFunction)app_jump_addr;
        
        // 5. 跳转
        JumpToApplication();
    } else {
        printf("Invalid App Stack Pointer! Halt.\r\n");
        while(1); // 死机，等待重刷
    }
}

/*
 * Bootloader 主状态机
 * 在 main() 的 while(1) 之前调用
 */
void Boot_MainStateMachine(void) {
    printf("\r\n--- Bootloader Started ---\r\n");
    
    // 1. 初始化标志区 (会读取状态)
    OTA_Manager_Init();
    OTA_Flag_t *flag = OTA_Get_Flag();
    
    // 2. 如果标志区还是首次运行(无魔数)，或者状态是 IDLE，且已经有下载好的固件
    if (flag->current_state == OTA_STATE_DOWNLOAD_DONE) {
        // 检查外部 Flash 中是否有合法的固件 (通过读取外部 Flash 看是否为空)
        uint32_t ext_first_word = 0;
        ExtFlash_Driver.read(EXT_OTA_DOWNLOAD_ADDR, (uint8_t*)&ext_first_word, 4);
        
        if (ext_first_word != 0xFFFFFFFF && ext_first_word != 0x00000000) {
            printf("Found firmware in external Flash. Updating...\r\n");
            Boot_UpdateAndJump(); // 搬运后会自动重启
        } else {
            printf("Error: No valid firmware found in external Flash.\r\n");
        }
    }
    
    // 3. 如果状态是 TESTING，说明上次刚搬运完，需要判断新固件是否成功启动
    if (flag->current_state == OTA_STATE_TESTING) {
        flag->boot_retry_cnt++; // 每次上电加 1
        
        if (flag->boot_retry_cnt > 3) {
            // 连续 3 次启动失败，新固件是砖块！
            printf("New firmware failed to boot 3 times! Rolling back...\r\n");
            // 回滚逻辑：把备份区的固件重新搬到运行区 (类似上面的逻辑)
            // 这里暂时简写，先直接死机等待人工介入，或者标记状态后重新搬运
            flag->current_state = OTA_STATE_ROLLBACK;
            OTA_Update_Flag(flag);
            while(1); // 待实现回滚逻辑
        } else {
            OTA_Update_Flag(flag); // 保存重试次数
            printf("Testing new firmware (attempt %lu/3)...\r\n", (unsigned long)flag->boot_retry_cnt);
        }
    }

    // 4. 正常情况，直接跳转 APP
    Boot_JumpToApp();
}