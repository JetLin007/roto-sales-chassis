# ============================================================================
# CMakeLists.txt - roto_sales_chassis_boot
# ============================================================================
cmake_minimum_required(VERSION 3.22)

# 交叉编译环境
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# 工程名
set(CMAKE_PROJECT_NAME roto_sales_chassis_boot)
project(${CMAKE_PROJECT_NAME} C ASM)

# C 标准
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 芯片架构与编译选项
add_compile_options(
    -mcpu=cortex-m4
    -mthumb
    -mfpu=fpv4-sp-d16
    -mfloat-abi=hard
    -Wall -Wextra -Wno-unused-parameter
    -ffunction-sections -fdata-sections
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-O0 -g3 -gdwarf-4)
else()
    add_compile_options(-O2)
endif()

# 🌟 关键宏定义：只定义 TARGET_BOOTLOADER
add_compile_definitions(
    USE_HAL_DRIVER
    STM32G474xx
    PLATFORM_STM32G474
    TARGET_BOOTLOADER
)

# 头文件包含路径 (不含 FreeRTOS)
include_directories(
    Core/Inc
    Drivers/STM32G4xx_HAL_Driver/Inc
    Drivers/STM32G4xx_HAL_Driver/Inc/Legacy
    Drivers/CMSIS/Device/ST/STM32G4xx/Include
    Drivers/CMSIS/Include
    
    App
    Port
    Common
)

# 收集并过滤源文件
file(GLOB_RECURSE ALL_C_SOURCES
    "Core/Src/*.c"
    "Drivers/STM32G4xx_HAL_Driver/Src/*.c"
    "App/*.c"
    "Port/*.c"
)

# 黑名单（排除模板和冲突文件）
set(FILES_TO_EXCLUDE
    "${CMAKE_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_timebase_tim_template.c"
    "${CMAKE_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_timebase_rtc_alarm_template.c"
    "${CMAKE_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_timebase_rtc_wakeup_template.c"
    "${CMAKE_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_msp_template.c"
)

foreach(FILE ${FILES_TO_EXCLUDE})
    list(REMOVE_ITEM ALL_C_SOURCES ${FILE})
endforeach()

# 启动文件
set(STARTUP_ASM "${CMAKE_SOURCE_DIR}/startup_stm32g474xx.s")
set(ALL_SOURCES ${ALL_C_SOURCES} ${STARTUP_ASM})

# 生成可执行文件
add_executable(${CMAKE_PROJECT_NAME}.elf ${ALL_SOURCES})

# 链接选项 (使用 Boot 专属的链接脚本，保留内存统计打印)
target_link_options(${CMAKE_PROJECT_NAME}.elf PRIVATE
    -mcpu=cortex-m4
    -mthumb
    -mfpu=fpv4-sp-d16
    -mfloat-abi=hard
    -T${CMAKE_SOURCE_DIR}/STM32G474VETx_FLASH.ld
    -Wl,--print-memory-usage
    -Wl,--gc-sections
    -Wl,-Map=${CMAKE_PROJECT_NAME}.map
    -specs=nosys.specs
    -specs=nano.specs
    -lm
)

# 生成 BIN/HEX 并打印 Size
add_custom_command(TARGET ${CMAKE_PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}.elf> ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${CMAKE_PROJECT_NAME}.elf> ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${CMAKE_PROJECT_NAME}.elf>
    COMMENT "Generating BIN/HEX..."
)