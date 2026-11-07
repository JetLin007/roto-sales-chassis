#include "app_ymodem.h"
#include "app_ota_download.h"
#include "port_uart.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

extern UART_HandleTypeDef huart3;

/* 全局 OTA 模式标志：0=Modbus 模式，1=YModem 模式(由 app_freertos 与 app_modbus 共享) */
volatile uint8_t g_is_ota_mode = 0;

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
static int ym_dbg_first = 1;   // 只打印首次接收返回码/句柄状态，便于诊断

// 发送单字节 (适配 RS485 半双工)
static void ym_send_byte(uint8_t b) {
    // 假设你用 USART3 作为 RS485
    Port_UART_RS485_SendByte(b);
    printf("[YModem] TX byte: 0x%02X\r\n", b);
}

void App_YModem_Init(void) {
    ym_state = YM_STATE_WAIT_START;
    ym_file_size = 0;
    ym_current_offset = 0;
    ym_dbg_first = 1;
    printf("[YModem] Init done, sending 'C'...\r\n");
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

// ============ 任务上下文：固定长度整包接收 (不依赖 IDLE 断包) ============
#define YM_TIMEOUT_MS   2000u  // 单包最长等待时间
static uint8_t packet_buf[1029];         // 整包缓冲: 帧头+号+反码+数据+CRC
static uint8_t g_header_done = 0;        // 是否已收文件名头
#define YM_RX_QUEUE_SIZE 2048u
static uint8_t ym_rx_queue[YM_RX_QUEUE_SIZE];
static volatile uint16_t ym_rx_head;
static volatile uint16_t ym_rx_tail;
static volatile uint32_t ym_rx_overflow_count;

/* USART3 RX ISR producer; YModem task is the consumer. */
void App_YModem_RxByte(uint8_t byte) {
    uint16_t next = (uint16_t)((ym_rx_head + 1u) & (YM_RX_QUEUE_SIZE - 1u));
    if (next != ym_rx_tail) {
        ym_rx_queue[ym_rx_head] = byte;
        __DMB();
        ym_rx_head = next;
    } else {
        ym_rx_overflow_count++;
    }
}

static uint8_t ym_read_byte(uint8_t *byte, uint32_t timeout_ms) {
    uint32_t start = HAL_GetTick();

    while (ym_rx_tail == ym_rx_head) {
        if ((HAL_GetTick() - start) >= timeout_ms) {
            return 0;
        }
        HAL_Delay(1);
    }

    __DMB();
    *byte = ym_rx_queue[ym_rx_tail];
    ym_rx_tail = (uint16_t)((ym_rx_tail + 1u) & (YM_RX_QUEUE_SIZE - 1u));
    return 1;
}

/* 发送 ACK + 'C' */
static void ym_send_ack_c(void) {
    uint8_t b[2] = {ACK, CRC16};
    Port_UART_RS485_SendBuf(b, 2);
    printf("[YModem] TX ACK+'C'\r\n");
}

/* 进入 OTA 模式后调用：放弃 DMA+IDLE，改为任务上下文阻塞式整包接收 */
void App_YModem_StartDMA(void) {
    /* 注意：这里不再做 HAL_UART_DeInit/Init。DeInit 会把 PB10/PB11 引脚短暂置于悬空态，
       半双工 485 的方向电路若靠 TX 线判定方向，可能被这个毛刺锁在发送态，
       之后就一直收不到上位机的数据（表现为只收到 1~2 个字节）。
       句柄复位已由前面的 Port_UART_Reset() 完成，这里只需清标志后挂上中断接收。 */
    __HAL_UART_DISABLE_IT(&huart3, UART_IT_IDLE);
    __HAL_UART_CLEAR_IDLEFLAG(&huart3);
    ym_rx_head = 0;
    ym_rx_tail = 0;
    ym_rx_overflow_count = 0;
    Port_UART_RS485_Init();
}

/* OTA 模式下 IDLE 回调本会调用；现改为阻塞式接收，此处无需处理 */
void App_YModem_RxEvent(uint16_t Size) {
    (void)Size;
}

/* 任务上下文：固定长度整包接收，解析 YModem 协议并回应 ACK/NAK */
void App_YModem_Process(void) {
    if (ym_state == YM_STATE_IDLE) return;

    uint8_t hdr;
    if (!ym_read_byte(&hdr, YM_TIMEOUT_MS)) return;

    if (hdr == EOT) {            // 数据区结束
        ym_send_ack_c();         // ACK + 'C'，等待收尾空块
        g_header_done = 1;
        printf("[YModem] EOT\r\n");
        return;
    }
    if (hdr == CAN) { ym_state = YM_STATE_IDLE; return; }

    uint16_t dlen;
    if (hdr == SOH) dlen = 128;
    else if (hdr == STX) dlen = 1024;
    else { ym_send_byte(NAK); return; }   // 非法帧头

    uint16_t flen = dlen + 5;
    packet_buf[0] = hdr;
    /* ⚠ 关键：从读到帧头到收完余下字节之间，绝不可插入任何 printf/慢速打印!
       先连续收完整帧再打印。临时改为逐字节统计，判定 PC 实际送达的字节数。 */
    uint16_t got = 0;
    while (got < flen - 1) {
        uint8_t b;
        if (!ym_read_byte(&b, 300)) break; // 300ms 无更多字节则停止
        packet_buf[1 + got] = b;
        got++;
    }
    printf("[YModem] hdr=0x%02X got=%u/%u\r\n", hdr, (unsigned)got, (unsigned)(flen - 1));
    for (int dbg = 0; dbg < (int)got; dbg++) {
        if (dbg % 16 == 0) printf("\r\n  ");
        printf("%02X ", packet_buf[1 + dbg]);
    }
    if (got != flen - 1) {
        uint32_t rx_count, rearm_fail_count, last_error, last_rearm_status;
        uint32_t rx_during_tx;
        uint8_t pb10_level;
        uint32_t cr1;
        Port_UART_GetRxDiagnostics(&rx_count, &rearm_fail_count,
                                   &last_error, &last_rearm_status,
                                   &rx_during_tx);
        Port_UART_GetTxDiagnostics(&pb10_level, &cr1);
        printf("\r\n[YModem] RX diag: callbacks=%lu rearm_fail=%lu "
               "last_err=0x%08lX last_arm=%lu overflow=%lu "
               "rx_in_tx=%lu PB10=%u CR1=0x%08lX RxState=%u\r\n",
               (unsigned long)rx_count, (unsigned long)rearm_fail_count,
               (unsigned long)last_error, (unsigned long)last_rearm_status,
               (unsigned long)ym_rx_overflow_count,
               (unsigned long)rx_during_tx,
               (unsigned)pb10_level, (unsigned long)cr1,
               (unsigned)huart3.RxState);
        ym_send_byte(NAK);
        return;
    }
    ym_dbg_first = 0;

    uint8_t pkn = packet_buf[1], pinv = packet_buf[2];
    if ((uint8_t)(pkn ^ pinv) != 0xFF) { ym_send_byte(NAK); return; }

    uint16_t crc = 0;
    for (int i = 3; i < 3 + dlen; i++) crc = crc16_update(crc, packet_buf[i]);
    uint16_t crc_recv = (uint16_t)((packet_buf[3 + dlen] << 8) | packet_buf[3 + dlen + 1]);
    if (crc != crc_recv) {
        uint16_t nz = 0;
        for (int i = 3; i < 3 + dlen; i++) if (packet_buf[i] != 0) nz++;
        printf("[YModem] CRCFAIL hdr=%02X pkn=%u dlen=%u nz=%u calc=%04X recv=%04X\r\n",
               hdr, pkn, dlen, (unsigned)nz, crc, crc_recv);
        printf("  dump:=");
        for (int dbg=0; dbg<160; dbg++) {
            if (dbg%16==0) printf("\r\n    ");
            printf("%02X ", packet_buf[dbg]);
        }
        printf("\r\n");
        printf("  str=%s\r\n", (const char*)&packet_buf[3]);
        printf("  tail: d[126..129]=%02X %02X %02X %02X crc@128=%04X crc@1024=%04X\r\n",
               packet_buf[126],packet_buf[127],packet_buf[128],packet_buf[129],
               (unsigned)((packet_buf[131]<<8)|packet_buf[132]),
               (unsigned)((packet_buf[1023]<<8)|packet_buf[1024]));
        ym_send_byte(NAK);
        return;
    }

    /* 包号 0：可能是 ① 文件名头 ② 包号回绕的数据块 ③ 收尾空块 */
    if (pkn == 0) {
        if (ym_current_offset == 0) {
            /* ① 文件名头（仅第一个包） */
            char *fn = (char *)&packet_buf[3];
            if (fn[0] == 0) {
                /* 空文件名，当作结束 */
                ym_send_byte(ACK);
                ym_send_byte(CAN); ym_send_byte(CAN); ym_send_byte(CAN); ym_send_byte(CAN);
                App_OTA_FinishDownload(ym_file_size, 0);
                printf("[YModem] Done, size=%lu\r\n", ym_file_size);
                ym_state = YM_STATE_IDLE;
            } else {
                char *sz = fn + strlen(fn) + 1;
                if (strlen(sz) > 0) sscanf(sz, "%lu", &ym_file_size);
                printf("[YModem] File: %s, Size: %lu\r\n", fn, ym_file_size);
                ym_send_ack_c();                         // ACK + 'C' 请求数据
            }
            return;
        }

        /* ②/③ 包号回绕：检查数据是否全 0 来区分收尾空块和数据包 */
        int all_zero = 1;
        for (int i = 3; i < 3 + dlen; i++) {
            if (packet_buf[i] != 0) { all_zero = 0; break; }
        }

        if (all_zero) {
            /* ③ 收尾空块（EOT 之后） */
            ym_send_byte(ACK);
            ym_send_byte(CAN); ym_send_byte(CAN); ym_send_byte(CAN); ym_send_byte(CAN);
            App_OTA_FinishDownload(ym_file_size, 0);
            printf("[YModem] Done, size=%lu\r\n", ym_file_size);
            ym_state = YM_STATE_IDLE;
        } else {
            /* ② 数据包（包号回绕到 0），正常处理 */
            if (ym_current_offset + dlen > EXT_DOWNLOAD_MAX_SIZE) { ym_send_byte(CAN); return; }
            App_OTA_HandleRxData(ym_current_offset, &packet_buf[3], dlen);
            ym_current_offset += dlen;
            ym_send_byte(ACK);
            printf("[YModem] pkn=%u(wrap) off=%lu/%lu\r\n", pkn, ym_current_offset, ym_file_size);
        }
        return;
    }

    /* 数据块：写入外部 Flash */
    if (ym_current_offset + dlen > EXT_DOWNLOAD_MAX_SIZE) { ym_send_byte(CAN); return; }
    App_OTA_HandleRxData(ym_current_offset, &packet_buf[3], dlen);
    ym_current_offset += dlen;

    ym_send_byte(ACK);
    printf("[YModem] pkn=%u off=%lu/%lu\r\n", pkn, ym_current_offset, ym_file_size);
}
