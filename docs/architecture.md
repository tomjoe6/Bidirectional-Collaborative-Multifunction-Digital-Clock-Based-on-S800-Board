# S800 Three-Party Architecture

This directory contains a simplified three-party architecture diagram for the
S800 bidirectional collaborative multifunction digital clock.

- `architecture.svg`: directly viewable block diagram.
- `architecture.png`: PNG export of the architecture diagram.
- `architecture.jpg`: JPG export of the architecture diagram.
- `architecture.mmd`: Mermaid source for future edits.

## High-Level Flow

```mermaid
flowchart LR
    USER["用户方"] -->|"查看/按键/控制"| S800["S800板端"]
    USER -->|"操作界面"| PC["PC上位机"]
    S800 <-->|"USB虚拟串口\n命令/事件/心跳"| PC
    PC <-->|"NTP/天气"| NET["互联网"]
```

## Detailed Diagram

Open `docs/architecture.svg` in VSCode, a browser, or any SVG viewer.

The source Mermaid diagram is available in `docs/architecture.mmd`.
