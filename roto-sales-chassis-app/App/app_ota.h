#ifndef __APP_OTA_H__
#define __APP_OTA_H__

#include <stdint.h>
#include "ota_common.h"

/* 在 APP 启动时调用，用于确认新固件是否正常，并清除测试标志 */
void App_OTA_CheckAndConfirm(void);

/* 接收上位机升级指令时调用，准备重启进入 Bootloader */
void App_OTA_RequestUpdate(void);

#endif /* __APP_OTA_H__ */