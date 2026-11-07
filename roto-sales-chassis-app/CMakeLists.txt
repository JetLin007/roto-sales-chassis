# ============================================================================
# CMakeLists.txt - STM32G474 APP / BOOT 构建脚本
# ============================================================================
cmake_minimum_required(VERSION 3.22)

# 1. 在 CMake 中定义版本号变量 (以后发版只改这里)
set(FW_VER_MAJOR 1)
set(FW_VER_MINOR 0)
set(FW_VER_PATCH 0)

# 2. 自动拼接出字符串版本号 (例如 "v1.0.0")
set(VERSION_STR "v${FW_VER_MAJOR}.${FW_VER_MINOR}.${FW_VER_PATCH}")

# 3. 将这几个数字传给 C 编译器，供代码中打印使用
add_compile_definitions(
    FW_VERSION_MAJOR=${FW_VER_MAJOR}
    FW_VERSION_MINOR=${FW_VER_MINOR}
    FW_VERSION_PATCH=${FW_VER_PATCH}
)

# 1. 交叉编译配置
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# 2. 工程定义
set(CMAKE_PROJECT_NAME roto_sales_chassis_app) # Boot 工程请改为 roto_sales_chassis_boot
project(${CMAKE_PROJECT_NAME} C ASM)

# 3. C 标准
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 4. 核心编译与链接参数 (Cortex-M4 FPU)
set(CPU_PARAMS -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard)

add_compile_options(
    ${CPU_PARAMS}
    -Wall -Wextra -Wno-unused-parameter
    -ffunction-sections -fdata-sections
)

# Debug / Release 优化
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-O0 -g3 -gdwarf-4)
else()
    add_compile_options(-O2)
endif()

# 5. 全局宏定义 (Boot 工程请把 TARGET_APP 改成 TARGET_BOOTLOADER)
add_compile_definitions(
    USE_HAL_DRIVER
    STM32G474xx
    PLATFORM_STM32G474
    TARGET_APP 
)

# 6. 头文件搜索路径 (包含新加的 App/Port/Common)
include_directories(
    Core/Inc
    Drivers/STM32G4xx_HAL_Driver/Inc
    Drivers/STM32G4xx_HAL_Driver/Inc/Legacy
    Drivers/CMSIS/Device/ST/STM32G4xx/Include
    Drivers/CMSIS/Include
    
    # 中间件: FreeRTOS (Boot 工程可以去掉)
    Middlewares/Third_Party/FreeRTOS/Source/include
    Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS
    Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2
    Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F
    
    # 自己的目录
    App
    Port
    Common
)

# ============================================================================
# 7. 收集并过滤源文件
# ============================================================================
# 1. 全量扫描所有可能用到的 C 文件
file(GLOB_RECURSE ALL_C_SOURCES
    "Core/Src/*.c"
    "Drivers/STM32G4xx_HAL_Driver/Src/*.c"
    "Middlewares/Third_Party/FreeRTOS/Source/*.c"
    "Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F/*.c"
    "Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS/*.c"
    "Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2/*.c"
    "App/*.c"
    "Port/*.c"
)

# 2. 定义需要排除的“黑名单”
set(FILES_TO_EXCLUDE
    # HAL 库中不需要的模板文件
    "${CMAKE_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_timebase_tim_template.c"
    "${CMAKE_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_timebase_rtc_alarm_template.c"
    "${CMAKE_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_timebase_rtc_wakeup_template.c"
    "${CMAKE_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_msp_template.c"
    
    # FreeRTOS 中不需要的堆管理文件 (我们只用 heap_4.c)
    "${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/heap_1.c"
    "${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/heap_2.c"
    "${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/heap_3.c"
    "${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/heap_5.c"
)

# 3. 执行过滤
foreach(FILE ${FILES_TO_EXCLUDE})
    list(REMOVE_ITEM ALL_C_SOURCES ${FILE})
endforeach()

# 4. 合并启动文件
set(STARTUP_ASM "${CMAKE_SOURCE_DIR}/startup_stm32g474xx.s")
set(ALL_SOURCES ${ALL_C_SOURCES} ${STARTUP_ASM})

# 启动文件 (根据根目录下实际的名字)
set(STARTUP_ASM "${CMAKE_SOURCE_DIR}/startup_stm32g474xx.s")

# ============================================================================
# 8. 生成可执行文件
# ============================================================================
add_executable(${CMAKE_PROJECT_NAME}.elf ${ALL_SOURCES} ${STARTUP_ASM})

# ============================================================================
# 9. 链接选项 (这里就是奇迹发生的地方)
# ============================================================================
target_link_options(${CMAKE_PROJECT_NAME}.elf PRIVATE
    ${CPU_PARAMS}
    -T${CMAKE_SOURCE_DIR}/STM32G474VETx_FLASH.ld
    -Wl,--print-memory-usage    # 🌟 这一行是自动打印内存占用的魔法！
    -Wl,--gc-sections
    -Wl,-Map=${CMAKE_PROJECT_NAME}.map
    -specs=nosys.specs
    -specs=nano.specs
    -lm
)

# ============================================================================
# 10. 后处理：生成 BIN / HEX 及尺寸统计
# ============================================================================
add_custom_command(TARGET ${CMAKE_PROJECT_NAME}.elf POST_BUILD
    # 1. 生成带版本号的 bin
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}.elf> ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}_${VERSION_STR}.bin
    
    # 2. 复制一份不带版本号的，专门给 OpenOCD 烧录脚本用
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}_${VERSION_STR}.bin ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
    
    # 3. 生成 hex 和打印大小
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${CMAKE_PROJECT_NAME}.elf> ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}_${VERSION_STR}.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${CMAKE_PROJECT_NAME}.elf>
    COMMENT "Generating BIN/HEX with version ${VERSION_STR}..."
)

# ============================================================================
# 11. 打印构建信息
# ============================================================================
message(STATUS "========================================")
message(STATUS "Project: ${CMAKE_PROJECT_NAME}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Binary Dir: ${CMAKE_BINARY_DIR}")
message(STATUS "========================================")
