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

/* --- 简易点灯调试函数 --- */
// 闪烁 times 次，每次间隔约 0.5 秒
void debug_blink_led(int times) {
    for (int i = 0; i < times; i++) {
        HAL_GPIO_WritePin(GPIOE, GPIO_PIN_0, GPIO_PIN_RESET); // 亮 (低电平点亮)
        for (volatile int j = 0; j < 5000000; j++);            // 粗糙延时
        HAL_GPIO_WritePin(GPIOE, GPIO_PIN_0, GPIO_PIN_SET);   // 灭
        for (volatile int j = 0; j < 5000000; j++);
    }
}

/* 
 * 核心功能：搬运固件并跳转
 * 从外部下载区读取，覆盖内部运行区
 */
void Boot_UpdateAndJump(void) {
    OTA_Flag_t *flag = OTA_Get_Flag();
    uint32_t total_size = flag->target_size;
    uint32_t total_crc = flag->target_crc;

    // 埋点 1：进入搬运函数，闪 1 下
    debug_blink_led(1);
    

    // 1. 擦除内部 App 运行区
    // 根据 APP_RUN_MAX_SIZE 计算需要擦除多少个 2KB 扇区
    for (uint32_t addr = APP_RUN_ADDR; addr < (APP_RUN_ADDR + total_size); addr += 2048) {
        // 埋点 2：准备擦除第一页，闪 2 下
        if (addr == APP_RUN_ADDR) debug_blink_led(2);

        if (Flash_Driver.erase_sector(addr) != FLASH_OK) {
            // 埋点 3：擦除失败，闪 3 下并死机
            debug_blink_led(3);
            while(1);
            return; // 擦除失败，直接死机，等待重新上电
        }
    }

    // 埋点 4：擦除完成，准备搬运数据，闪 4 下
    debug_blink_led(4);

    // 2. 从外部 SPI Flash 搬运数据到内部 Flash
    uint32_t copied_size = 0;
    while (copied_size < total_size) {
        uint32_t chunk_size = (total_size - copied_size) > COPY_BUFFER_SIZE ? COPY_BUFFER_SIZE : (total_size - copied_size);
        
        // 从外部读取
        ExtFlash_Driver.read(EXT_OTA_DOWNLOAD_ADDR + copied_size, copy_buffer, chunk_size);
        
        // 写入内部 (注意：G474 内部 Flash 必须 8 字节对齐写入)
        // 为了安全，补齐为 8 的倍数
        uint32_t write_len = (chunk_size + 7) & ~7; 
        if (Flash_Driver.write(APP_RUN_ADDR + copied_size, copy_buffer, write_len) != FLASH_OK) {
            // 埋点 5：写入失败，闪 5 下并死机
            debug_blink_led(5);
            while(1);
            return;
        }
        
        copied_size += chunk_size;
    }
    
    // 埋点 6：搬运成功，准备重启，闪 6 下
    //debug_blink_led(6);
    
    // 3. 更新标志区：进入测试状态，并清零重试次数
    flag->current_state = OTA_STATE_TESTING;
    flag->boot_retry_cnt = 0;
    OTA_Update_Flag(flag);
    
    // 埋点 7：更新标志区成功，准备重启
    debug_blink_led(7);
    HAL_Delay(100);
    NVIC_SystemReset();
}

/*
 * 核心功能：检查并跳转到 APP
 */
void Boot_JumpToApp(void) {
    uint32_t app_stack_pointer = *(volatile uint32_t*)(APP_RUN_ADDR);
    
    // 检查栈顶合法性
    if (app_stack_pointer >= 0x20000000 && app_stack_pointer <= 0x20020000) {

        // =============== 极其关键的清理工作 ===============
        
        // 1. 彻底关闭 SysTick
        SysTick->CTRL = 0;
        SysTick->LOAD = 0;
        SysTick->VAL = 0;
        
        // 2. 关闭所有 NVIC 中断，清除所有挂起标志位！
        for (uint8_t i = 0; i < 8; i++) {
            NVIC->ICER[i] = 0xFFFFFFFF; // 关闭所有中断使能
            NVIC->ICPR[i] = 0xFFFFFFFF; // 清除所有挂起的中断标志
        }
        
        // 3. （可选）如果 Bootloader 里开启了外设（SPI/USART等），可以在这里反初始化
        // HAL_DeInit(); 
        
        // 4. 设置 MSP
        __set_MSP(app_stack_pointer);

        SCB->VTOR = APP_RUN_ADDR;
        
        // 5. 内存屏障，确保以上操作全部生效
        __DSB();
        __ISB();

        // =============== 正式跳转 ===============

        // 取 APP 的 Reset_Handler 地址（bit0=1 表示 Thumb 模式）
        uint32_t app_jump_addr = *(volatile uint32_t*)(APP_RUN_ADDR + 4);

        // 关全局中断（最后一步，防止跳转瞬间被中断打断）
        __disable_irq();

        // 内联汇编直接跳转到 Reset_Handler：
        //   - 彻底避免 C 函数调用带来的压栈/返回地址污染
        //   - 先设置 MSP，再 BX 跳到 App 的 Reset_Handler
        __asm volatile (
            "mov r0, %0\n"         // r0 = app_jump_addr (Reset_Handler)
            "blx r0\n"              // 跳转（含 bit0=1 的 Thumb 模式切换）
            :
            : "r"(app_jump_addr)
            : "r0", "memory"
        );

        // 正常情况永远到不了这里
        while(1);
    } else {
        while(1){ /* 慢闪 */ }
    }
}

 /*
 * Bootloader 主状态机
 * 在 main() 的 while(1) 之前调用
 */
void Boot_MainStateMachine(void) {

    // 1. 初始化标志区 (会读取状态)
    OTA_Manager_Init();
    OTA_Flag_t *flag = OTA_Get_Flag();
    
    // 2. 如果标志区还是首次运行(无魔数)，或者状态是 IDLE，且已经有下载好的固件
    if (flag->current_state == OTA_STATE_DOWNLOAD_DONE) {
        // 检查外部 Flash 中是否有合法的固件 (通过读取外部 Flash 看是否为空)
        uint32_t ext_first_word = 0;
        ExtFlash_Driver.read(EXT_OTA_DOWNLOAD_ADDR, (uint8_t*)&ext_first_word, 4);
        
        if (ext_first_word != 0xFFFFFFFF && ext_first_word != 0x00000000) {
            Boot_UpdateAndJump(); // 搬运后会自动重启
        } else {
        }
    }
    
    // 3. 如果状态是 TESTING，说明上次刚搬运完，需要判断新固件是否成功启动
    if (flag->current_state == OTA_STATE_TESTING) {
        flag->boot_retry_cnt++; // 每次上电加 1
        
        if (flag->boot_retry_cnt > 3) {
            // 连续 3 次启动失败，新固件是砖块！
            // 回滚逻辑：把备份区的固件重新搬到运行区 (类似上面的逻辑)
            // 这里暂时简写，先直接死机等待人工介入，或者标记状态后重新搬运
            flag->current_state = OTA_STATE_ROLLBACK;
            OTA_Update_Flag(flag);
            while(1); // 待实现回滚逻辑
        } else {
            OTA_Update_Flag(flag); // 保存重试次数
        }
    }

    // 4. 正常情况，直接跳转 APP
    Boot_JumpToApp();
}