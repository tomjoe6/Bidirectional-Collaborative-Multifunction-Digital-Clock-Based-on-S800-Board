# S800 智能联网时钟系统

基于 TM4C1294NCPDT 的双向协作多功能数字时钟。板端独立运行时钟/闹钟/编辑，PC 端 USB 虚拟串口远程控制与 1:1 数字孪生镜像。

上海交通大学"嵌入式系统与接口技术"课程项目。

## 快速开始

**板端**：Keil 打开 `mcu/S.uvprojx` → 编译 → 烧录

**PC 端**：
```
venv虚拟环境下：
pip install -r pc_host/requirements.txt
python run.py
```

## 技术栈

| 端 | 技术 |
|:---|:---|
| 板端 | TM4C129 + TivaWare DriverLib + C89 |
| PC 端 | Python 3.11 + PyQt5 + pyserial |
