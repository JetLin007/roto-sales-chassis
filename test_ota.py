import serial
import time
import os
import sys
from ymodem.Socket import ModemSocket
from ymodem.Protocol import ProtocolType

# ================= 配置区 =================
COM_PORT = 'COM16'        # 请修改为你电脑上的 USB 转 485 串口号
BAUD_RATE = 115200
BIN_FILE = r'E:\workspace\Develop-Coding\roto-sales-chassis\roto-sales-chassis-app\build\roto_sales_chassis_app_v1.0.2.bin' # firmware path
# =========================================

def calc_crc16(data: bytes) -> bytes:
    """Modbus RTU CRC16 校验"""
    crc = 0xFFFF
    for pos in data:
        crc ^= pos
        for _ in range(8):
            if (crc & 1) != 0:
                crc >>= 1
                crc ^= 0xA001
            else:
                crc >>= 1
    return bytes([crc & 0xFF, (crc >> 8) & 0xFF])

def send_ota_command(ser):
    """Send OTA trigger command (0x41)"""
    payload = bytes([0x01, 0x41, 0x00, 0x00, 0x00, 0x00])
    frame = payload + calc_crc16(payload)
    print(f"[*] Send OTA trigger cmd: {frame.hex(' ').upper()}")
    ser.write(frame)
    time.sleep(0.5)

def format_size(n):
    """将字节数格式化为人类可读的大小"""
    if n < 1024:
        return f"{n} B"
    elif n < 1024 * 1024:
        return f"{n / 1024:.2f} KB"
    else:
        return f"{n / 1024 / 1024:.2f} MB"

def make_progress_callback():
    """创建进度回调，返回 callback 函数和状态字典"""
    state = {'start_time': None, 'last_sent': 0, 'last_time': None, 'speed': 0.0}

    def callback(task_index, task_name, total, sent):
        if state['start_time'] is None:
            state['start_time'] = time.perf_counter()
            state['last_time'] = state['start_time']
            state['last_sent'] = sent

        now = time.perf_counter()
        elapsed = now - state['start_time']

        # 计算瞬时速度 (基于最近一次回调的增量)
        dt = now - state['last_time'] if state['last_time'] else 0
        if dt > 0.05:  # 每 50ms 以上才更新一次速度，避免抖动
            ds = sent - state['last_sent']
            state['speed'] = ds / dt if dt > 0 else 0
            state['last_time'] = now
            state['last_sent'] = sent

        # 平均速度
        avg_speed = sent / elapsed if elapsed > 0 else 0

        # 进度百分比
        percent = (sent / total * 100) if total > 0 else 0

        # 预计剩余时间
        eta = (total - sent) / avg_speed if avg_speed > 0 else 0

        # 进度条宽度
        bar_width = 40
        filled = int(bar_width * sent / total) if total > 0 else 0
        bar = '█' * filled + '░' * (bar_width - filled)

        # 构建进度行 (用 \r 回到行首覆盖显示)
        line = (
            f"\r  [{bar}] {percent:5.1f}%  "
            f"{format_size(sent)} / {format_size(total)}  "
            f"速度: {format_size(avg_speed)}/s  "
            f"剩余: {eta:.0f}s   "
        )
        sys.stdout.write(line)
        sys.stdout.flush()

        # 传输完成时换行
        if sent >= total:
            sys.stdout.write('\n')
            sys.stdout.flush()

    return callback

def start_ymodem_send(ser, file_path):
    """Send file via YModem"""
    if not os.path.exists(file_path):
        print(f"[!] Firmware file not found: {file_path}")
        return False

    file_size = os.path.getsize(file_path)
    print(f"[*] Start YModem send: {file_path}")
    print(f"[*] File size: {format_size(file_size)}")
    print()

    def reader(size, timeout=1):
        ser.timeout = timeout
        return ser.read(size)

    def writer(data, timeout=1):
        ser.write_timeout = timeout
        # 厂家建议：RS485 不适合一次发大量数据，拆成小块逐包发送，
        # 避免自动方向电路在长突发时提前释放总线导致帧体丢失。
        CHUNK = 8
        for i in range(0, len(data), CHUNK):
            ser.write(data[i:i + CHUNK])
            ser.flush()
            time.sleep(0.002)

    socket = ModemSocket(reader, writer, protocol_type=ProtocolType.YMODEM, packet_size=128)

    progress_cb = make_progress_callback()

    try:
        # socket.send 的 callback 签名: callback(task_index, task_name, total, sent)
        socket.send([file_path], callback=progress_cb)
        print("[+] YModem 传输完成！")
        return True
    except Exception as e:
        print(f"\n[-] YModem 传输失败: {e}")
        return False

if __name__ == '__main__':
    try:
        # 打开串口
        ser = serial.Serial(COM_PORT, BAUD_RATE, timeout=1)
        print(f"[*] Serial {COM_PORT} opened.")

        # 1. Send Modbus OTA command, wait for MCU to switch to YModem mode
        send_ota_command(ser)

        # 2. Let MCU finish mode switch and start sending 'C'
        time.sleep(1.0)

        # 3. ModemSocket.send() handles 'C' handshake internally
        success = start_ymodem_send(ser, BIN_FILE)

        ser.close()
        print("[*] Test finished.")
        sys.exit(0 if success else 1)

    except serial.SerialException as e:
        print(f"[!] Serial open failed: {e}")
        sys.exit(1)
