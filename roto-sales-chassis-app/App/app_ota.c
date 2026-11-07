#include "app_ota.h"
#include "ota_manager.h"
#include "stm32g4xx_hal.h" // 为了调用 NVIC_SystemReset
#include <stdio.h>

/* 假设你有 RTOS 或者延时函数，这里为了演示简写 */
extern void delay_ms(uint32_t ms); 

typedef void (*BootEntry_t)(void);

static void App_OTA_JumpToBootloader(void) {
    uint32_t boot_stack = *(volatile uint32_t *)BOOTLOADER_START_ADDR;
    uint32_t boot_reset = *(volatile uint32_t *)(BOOTLOADER_START_ADDR + 4);

    if (boot_stack < 0x20000000 || boot_stack > 0x20020000 ||
        boot_reset < BOOTLOADER_START_ADDR || boot_reset >= OTA_FLAG_START_ADDR) {
        printf("[APP] ERROR: invalid bootloader vector sp=0x%08lX reset=0x%08lX\r\n",
               (unsigned long)boot_stack, (unsigned long)boot_reset);
        return;
    }

    printf("[APP] DOWNLOAD_DONE detected, jump to bootloader...\r\n");
    HAL_Delay(50);

    __disable_irq();
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;

    for (uint8_t i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }

    HAL_DeInit();
    SCB->VTOR = BOOTLOADER_START_ADDR;
    __set_MSP(boot_stack);
    __DSB();
    __ISB();

    ((BootEntry_t)boot_reset)();
    while (1) {
    }
}

void App_OTA_CheckAndConfirm(void) {
    OTA_State_t current_state = OTA_Get_State();
    
    // 如果复位后没有先进 bootloader，这里兜底主动跳转，让 bootloader 执行搬运
    if (current_state == OTA_STATE_DOWNLOAD_DONE) {
        App_OTA_JumpToBootloader();
        return;
    }

    // 如果上一次是搬运完新固件，处于测试状态
    if (current_state == OTA_STATE_TESTING) {
        // 在这里做一些系统级的自检（比如外设是否初始化成功，电机是否正常）
        // 如果自检通过：
        printf("[APP] Firmware self-test passed, confirming OTA state...\r\n");
        
        OTA_Flag_t *flag = OTA_Get_Flag();
        flag->boot_retry_cnt = 0; // 清除重试计数
        flag->current_state = OTA_STATE_CONFIRMED; // 标记为已确认
        
        // 写入 Flash 标志区
        OTA_Update_Flag(flag);
    } 
    else if (current_state == OTA_STATE_CONFIRMED || current_state == OTA_STATE_IDLE) {
        // 正常运行状态，啥也不用做
    }
}

void App_OTA_RequestUpdate(void) {
    printf("[APP] OTA request received, preparing to enter bootloader...\r\n");
    
    // 1. 从 RK3576 接收固件并写入外部 SPI Flash 的逻辑在此处触发
    // （这部分我们稍后配合通信协议写）
    
    // 2. 假设固件已经完全安全地下载到了外部 SPI Flash，更新标志区
    OTA_Flag_t *flag = OTA_Get_Flag();
    flag->current_state = OTA_STATE_DOWNLOAD_DONE;
    // 这里可以加上目标固件的 size 和 CRC，供 Bootloader 校验
    // flag->target_size = xxx;
    // flag->target_crc = xxx;
    
    OTA_Update_Flag(flag);
    
    // 3. 延时等待串口打印完毕，然后执行软复位，跳转进 Bootloader
    delay_ms(100);
    
    // 4. 软复位系统
    HAL_NVIC_SystemReset();
}
