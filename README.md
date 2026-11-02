---

# 🚀 Roto Sales Chassis (AGV 差速底盘控制系统)

基于 STM32G474 的两轮差速 AGV 控制系统，采用现代 CMake 构建体系，支持 RS485/CAN 通信以及基于外部 SPI Flash 的高可靠 OTA 固件升级。

## 🛠️ 硬件架构
*   **主控芯片**：STM32G474VET6 (Cortex-M4, 170MHz, 512KB Flash, 128KB RAM)
*   **通信接口**：
    *   **RS485 (USART3)**：与上位机 (RK3576) 通信 (Modbus RTU)
    *   **CAN (FDCAN1)**：控制伺服驱动器 (CANopen 协议)
*   **存储**：板载 16MB SPI Flash (W25Q128)

## 📂 工程结构
本仓库包含两个完全独立的子工程，均使用独立的 `CMakePresets.json` 进行构建：
*   `roto-sales-chassis-app`：主应用程序（运行 FreeRTOS，负责电机控制、通信及固件下载）。
*   `roto-sales-chassis-boot`：引导程序（裸机运行，负责固件搬运、跳转及防变砖回滚）。
*   `Common` / `Port` / `App`：属于两个工程共享的跨平台抽象层与业务逻辑。

## 💾 OTA Flash 分区规划

### 1. 内部 Flash (STM32G474 512KB)
*   `0x08000000 - 0x0800FFFF` (64KB)：**Bootloader** 引导程序。
*   `0x08010000 - 0x080107FF` (2KB)：**OTA 标志区**（存储状态、重试计数、CRC等）。
*   `0x08010800 - 0x0807FFFF` (446KB)：**APP 运行区**（唯一固定运行地址）。

### 2. 外部 SPI Flash (W25Q128 16MB)
*   `0x000000 - 0x0FFFFF` (1MB)：**OTA 下载缓存区**（临时存放 RK3576 下发的固件）。
*   `0x100000 - 0x1FFFFF` (1MB)：**出厂/安全备份区**（存放上一版安全固件，用于回滚）。

## 🛡️ 安全防变砖机制 (Rollback)
1. 新固件先通过 RS485 下载并保存在**外部 SPI Flash**。
2. 接收完成且校验通过后，写入标志区，重启。
3. Bootloader 将新固件搬运到内部运行区，并在标志区记录“测试状态 (TESTING)”。
4. 新固件启动后必须进行自检，自检通过后向标志区写入“确认状态 (CONFIRMED)”。
5. 若新固件启动失败并连续复位 3 次，Bootloader 将拒绝跳转，并从外部备份区恢复旧固件（待实现）。

## 💻 开发环境搭建

### 必备工具
1.  **GCC 交叉编译链**：`arm-gnu-toolchain-15.3.rel2-mingw-w64-x86_64-arm-none-eabi` (极其严格，代码必须规范)
2.  **构建工具**：CMake (推荐 3.27+) 与 Ninja
3.  **调试器**：OpenOCD + 正点原子 DAP / ST-Link
4.  **IDE**：VS Code (配合 CMake Tools 与 Cortex-Debug 插件)

### 编译方法
进入对应的工程目录（`app` 或 `boot`），打开终端：

```bash
# 1. 生成构建文件 (自动读取 CMakePresets.json 中的编译器路径)
cmake --preset gcc-arm

# 2. 编译项目
cmake --build build
```

编译结束后，终端会自动打印出极简的 `Memory region` 占用表。

### 烧录与调试
在 VS Code 中按下 `F5`，或选择左侧“运行和调试”面板中的 `Debug APP` 或 `Debug BOOT`。配置会自动调用 OpenOCD 进行烧录。

## 🚀 开发进度记录
- [x] 搭建基于 CMake + VS Code 的现代双工程编译链 (GCC 15.3)
- [x] 完成内部 Flash 驱动与外部 SPI Flash (W25Q128) 的跨平台抽象层
- [x] 完成 OTA 标志区的读写管理模块
- [x] 实现 Bootloader 的搬运、重试计数与跳转逻辑
- [x] 实现 APP 端的固件接收缓存到外部 Flash 机制
- [ ] **实现 RS485 (Modbus) 固件传输协议** (进行中)
- [ ] 联调完整 OTA 闭环与回滚测试
```

---
