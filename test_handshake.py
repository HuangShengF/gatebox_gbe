#!/usr/bin/env python3
"""
GBE3 握手测试工具
使用方法：python test_handshake.py COM3
"""

import serial
import struct
import sys

def crc16_ccitt_false(data):
    """CRC-16/CCITT-FALSE"""
    crc = 0xFFFF
    for byte in data:
        crc ^= (byte << 8)
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc = crc << 1
        crc &= 0xFFFF
    return crc

def pack_frame(command, sequence, payload):
    """打包帧"""
    magic = 0xB3A5
    payload_size = len(payload)

    # 构建头部和 Payload
    frame = struct.pack('<HHHH', magic, sequence, command, payload_size)
    frame += payload

    # 计算 CRC
    crc = crc16_ccitt_false(frame)
    frame += struct.pack('<H', crc)

    return frame

def unpack_frame(data):
    """解析帧"""
    if len(data) < 10:
        return None

    magic, seq, cmd, psize = struct.unpack('<HHHH', data[:8])

    if magic != 0xB3A5:
        return None

    if len(data) < 8 + psize + 2:
        return None

    payload = data[8:8+psize]
    crc_recv = struct.unpack('<H', data[8+psize:8+psize+2])[0]

    # 验证 CRC
    crc_calc = crc16_ccitt_false(data[:8+psize])

    if crc_calc != crc_recv:
        print(f"[ERROR] CRC 校验失败: 计算={crc_calc:04X}, 接收={crc_recv:04X}")
        return None

    return {
        'magic': magic,
        'sequence': seq,
        'command': cmd,
        'payload_size': psize,
        'payload': payload,
        'crc': crc_recv
    }

def send_handshake(ser):
    """发送握手请求"""
    # Handshake Request Payload
    payload = struct.pack('<BBHLL',
        1,      # Protocol Major
        0,      # Protocol Minor
        2048,   # Host Max Payload Size
        0,      # Host Capability Flags
    )

    frame = pack_frame(0x0001, 1, payload)

    print(f"[发送] 握手请求 ({len(frame)} bytes)")
    print(f"       Hex: {frame.hex(' ').upper()}")
    ser.write(frame)

def parse_handshake_response(payload):
    """解析握手响应"""
    if len(payload) < 58:
        print(f"[ERROR] Payload 长度不正确: {len(payload)} (期望 58)")
        return

    proto_major = payload[0]
    proto_minor = payload[1]
    device_model = payload[2]
    reserved = payload[3]
    device_max_payload = struct.unpack('<H', payload[4:6])[0]
    device_capability = struct.unpack('<L', payload[6:10])[0]

    hw_ver = payload[10:26].decode('ascii').rstrip('\x00')
    fw_ver = payload[26:42].decode('ascii').rstrip('\x00')
    serial_num = payload[42:58].decode('ascii').rstrip('\x00')

    device_names = {1: 'GBP3', 2: 'GBE3', 3: 'GBC3'}

    print("\n[接收] 握手响应:")
    print(f"  协议版本: {proto_major}.{proto_minor}")
    print(f"  设备型号: {device_names.get(device_model, 'Unknown')} ({device_model})")
    print(f"  最大 Payload: {device_max_payload} bytes")
    print(f"  能力标志: 0x{device_capability:08X}")
    print(f"  硬件版本: {hw_ver}")
    print(f"  固件版本: {fw_ver}")
    print(f"  序列号:   {serial_num}")

def main():
    if len(sys.argv) < 2:
        print("使用方法: python test_handshake.py <COM端口>")
        print("例如: python test_handshake.py COM3")
        sys.exit(1)

    port = sys.argv[1]

    print(f"正在打开 {port} (115200 bps)...\n")

    try:
        ser = serial.Serial(port, 115200, timeout=2)

        # 发送握手
        send_handshake(ser)

        # 等待响应
        print("\n等待响应...")

        # 接收数据
        rx_data = ser.read(1000)

        if len(rx_data) == 0:
            print("[ERROR] 没有收到响应")
            ser.close()
            return

        print(f"\n收到 {len(rx_data)} bytes:")
        print(f"Hex: {rx_data.hex(' ').upper()}")

        # 解析帧
        frame = unpack_frame(rx_data)

        if frame is None:
            print("\n[ERROR] 帧解析失败")
            ser.close()
            return

        print(f"\n帧信息:")
        print(f"  Magic:    0x{frame['magic']:04X}")
        print(f"  Sequence: {frame['sequence']}")
        print(f"  Command:  0x{frame['command']:04X}")
        print(f"  Payload:  {frame['payload_size']} bytes")
        print(f"  CRC:      0x{frame['crc']:04X} ✓")

        # 判断响应类型
        if frame['command'] == 0x1001:
            parse_handshake_response(frame['payload'])
            print("\n✅ 握手成功！")
        elif frame['command'] == 0x3001:
            err_code = struct.unpack('<H', frame['payload'][0:2])[0]
            err_detail = struct.unpack('<H', frame['payload'][2:4])[0]
            print(f"\n❌ 握手失败:")
            print(f"  错误码: 0x{err_code:04X}")
            print(f"  详情:   {err_detail}")
        else:
            print(f"\n❓ 未知响应命令: 0x{frame['command']:04X}")

        ser.close()

    except serial.SerialException as e:
        print(f"[ERROR] 串口错误: {e}")
    except Exception as e:
        print(f"[ERROR] {e}")

if __name__ == '__main__':
    main()
