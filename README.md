# S800 智能联网时钟系统

基于 TM4C1294NCPDT 的双向协作多功能数字时钟。板端独立运行时钟/闹钟/编辑，PC 端 USB 虚拟串口远程控制与 1:1 数字孪生镜像。

上海交通大学"嵌入式系统与接口技术"课程项目。

## 快速开始

**板端**：Keil 打开 `mcu/S.uvprojx` → 编译 → 烧录

**PC 端**：
```bash
pip install -r pc_host/requirements.txt
python run.py
```

## 文档索引

| 文档 | 内容 |
|:---|:---|
| [PRD.md](PRD.md) | 产品需求规格，功能基线 |
| [PROJECT.md](PROJECT.md) | 文件职责说明、使用指南、故障排查 |
| [docs/扩展功能_祝福与语音.md](docs/扩展功能_祝福与语音.md) | 节日祝福 + 语音指令 |
| [PLAN_综合修复.md](PLAN_综合修复.md) | 开发修复记录 |

## 技术栈

| 端 | 技术 |
|:---|:---|
| 板端 | TM4C129 + TivaWare DriverLib + C89 |
| PC 端 | Python 3.11 + PyQt5 + pyserial |
