# pc_app/serial_tester.py
import serial
import serial.tools.list_ports
import time
import sys

def list_com_ports():
    ports = [p.device for p in serial.tools.list_ports.comports()]
    return ports if ports else ["未检测到串口"]

def main():
    print("🔍 正在扫描可用串口...")
    coms = list_com_ports()
    print("可用端口:", coms)
    
    port = input("\n请输入串口号（如 COM3，回车自动选第一个）: ").strip() or coms[0]
    try:
        ser = serial.Serial(port, 115200, timeout=1)
        print(f"\n✅ 已连接 {port}，波特率 115200")
        print("输入指令（如 AT / S3=01 / S5?），输入 'quit' 退出\n")
        
        while True:
            cmd = input("> ").strip()
            if cmd.lower() == 'quit':
                break
            if not cmd:
                continue
            
            # 发送指令（自动加 \r\n）
            ser.write((cmd + '\r\n').encode('utf-8'))
            time.sleep(0.05)  # 给板子响应时间
            
            # 读取所有可用数据
            if ser.in_waiting:
                raw = ser.read_all()
                resp = raw.decode('utf-8', errors='replace')
                print(f"← {repr(resp)}")  # 显示原始字符（含 \r\n）
                # 可选：打印十六进制
                # print(f"← HEX: {raw.hex(' ')}")
            else:
                print("← 无响应（超时）")
                
    except Exception as e:
        print(f"❌ 错误: {e}")
    finally:
        if 'ser' in locals() and ser.is_open:
            ser.close()
            print("\n🔌 串口已关闭")

if __name__ == "__main__":
    main()