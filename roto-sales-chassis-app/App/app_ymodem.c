#include "app_ymodem.h"
#include "app_ota_download.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

#define SOH  0x01  // 128字节数据包
#define STX  0x02  // 1024字节数据包
#define EOT  0x04  // 结束传输
#define ACK  0x06  // 应答
#define NAK  0x15  // 否认
#define CAN  0x18  // 取消
#define CRC16 0x43 // 请求 CRC 模式 ('C')

typedef enum {
    YM_STATE_IDLE,
    YM_STATE_WAIT_START,
    YM_STATE_RECEIVING,
    YM_STATE_FINISHED
} YM_State_t;

static YM_State_t ym_state = YM_STATE_IDLE;
static uint32_t ym_file_size = 0;
static uint32_t ym_current_offset = 0;

// 发送单字节 (适配 RS485 半双工)
static void ym_send_byte(uint8_t b) {
    // 假设你用 USART3 作为 RS485
    extern UART_HandleTypeDef huart3;
    HAL_UART_Transmit(&huart3, &b, 1, 100);
}

void App_YModem_Init(void) {
    ym_state = YM_STATE_WAIT_START;
    ym_file_size = 0;
    ym_current_offset = 0;
    ym_send_byte(CRC16); // 发送 'C'，请求上位机开始发送
}

// 简单的 CRC16 校验算法
static uint16_t crc16_update(uint16_t crc, uint8_t data) {
    crc ^= (uint16_t)data << 8;
    for (int i = 0; i < 8; i++) {
        if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
        else crc <<= 1;
    }
    return crc;
}

// 接收缓冲区
static uint8_t packet_buf[1024 + 5]; // 数据 + 包号 + 包号反码 + CRC_H + CRC_L
static uint16_t packet_idx = 0;
static uint16_t expected_packet_size = 0;

void App_YModem_RxByte(uint8_t byte) {
    if (ym_state == YM_STATE_WAIT_START) {
        if (byte == SOH || byte == STX) {
            expected_packet_size = (byte == SOH) ? 128 : 1024;
            packet_buf[0] = byte;
            packet_idx = 1;
            ym_state = YM_STATE_RECEIVING;
        }
        return;
    }

    if (ym_state == YM_STATE_RECEIVING) {
        packet_buf[packet_idx++] = byte;
        
        // 收满一个完整包 (头 + 数据 + CRC16)
        if (packet_idx >= (expected_packet_size + 5)) {
            packet_idx = 0;
            ym_state = YM_STATE_WAIT_START;

            // 1. 提取包号
            uint8_t pkt_num = packet_buf[1];
            uint8_t pkt_num_inv = packet_buf[2];

            // 2. 校验包号反码
            if ((uint8_t)(pkt_num ^ pkt_num_inv) != 0xFF) {
                ym_send_byte(NAK);
                return;
            }

            // 3. 校验 CRC16
            uint16_t crc_calc = 0;
            for (int i = 3; i < expected_packet_size + 3; i++) {
                crc_calc = crc16_update(crc_calc, packet_buf[i]);
            }
            uint16_t crc_recv = (packet_buf[expected_packet_size + 3] << 8) | packet_buf[expected_packet_size + 4];
            
            if (crc_calc != crc_recv) {
                ym_send_byte(NAK);
                return;
            }

            // 4. 包号 0 处理 (文件名和大小信息)
            if (pkt_num == 0) {
                // YModem 包 0 的格式: "文件名\0文件大小(ASCII)\0..."
                char *file_info = (char *)&packet_buf[3];
                char *size_str = file_info + strlen(file_info) + 1;
                if (strlen(size_str) > 0) {
                    sscanf(size_str, "%lu", &ym_file_size);
                    printf("[YModem] File: %s, Size: %lu\r\n", file_info, ym_file_size);
                }
                ym_send_byte(ACK);
                ym_send_byte(CRC16); // 继续请求发数据
                return;
            }

            // 5. 实际数据包处理
            // 检查是否超出外部 Flash 下载区限制
            if (ym_current_offset + expected_packet_size > EXT_DOWNLOAD_MAX_SIZE) {
                ym_send_byte(CAN); // 取消传输
                return;
            }

            // 写入外部 Flash
            App_OTA_HandleRxData(ym_current_offset, &packet_buf[3], expected_packet_size);
            ym_current_offset += expected_packet_size;

            // 回复 ACK 请求下一包
            ym_send_byte(ACK);
        }
        
        // 处理 EOT (结束符)
        if (byte == EOT) {
            ym_send_byte(ACK); // 确认收到结束
            ym_send_byte(CRC16); // 请求下一个文件(如果有的化)
            
            printf("[YModem] Transfer Complete. Total: %lu\r\n", ym_current_offset);
            
            // 固件接收完毕，触发 OTA 完成逻辑
            App_OTA_FinishDownload(ym_file_size, 0); // CRC 暂传 0，真实情况需要边收边算
            ym_state = YM_STATE_IDLE;
        }
    }
}