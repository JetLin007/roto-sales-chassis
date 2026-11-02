import subprocess
import sys
import os

def main():
    if len(sys.argv) < 3:
        return
    
    size_exe = sys.argv[1]
    elf_file = sys.argv[2]
    bin_file = elf_file.replace('.elf', '.bin')
    
    try:
        # 调用 arm-none-eabi-size 获取输出
        output = subprocess.check_output([size_exe, elf_file], encoding='utf-8')
        lines = output.strip().split('\n')
        
        # 解析最后一行数据
        parts = lines[1].split()
        text = int(parts[0])
        data = int(parts[1])
        bss = int(parts[2])
        
        flash_bytes = text + data
        ram_bytes = data + bss
        
        # 计算 bin 文件实际大小
        bin_size = os.path.getsize(bin_file) if os.path.exists(bin_file) else 0
        
        # 打印面板
        print("\n===================================================")
        print("          Firmware Resource Usage Summary          ")
        print("===================================================")
        print(f"FLASH Usage : {flash_bytes/1024:.2f} KB (Text: {text} B, Data: {data} B)")
        print(f"BIN File    : {bin_size/1024:.2f} KB")
        print(f"RAM Usage   : {ram_bytes/1024:.2f} KB (Data: {data} B, BSS: {bss} B)")
        print("===================================================\n")
        
    except Exception as e:
        print(f"Size calculation failed: {e}")

if __name__ == '__main__':
    main()