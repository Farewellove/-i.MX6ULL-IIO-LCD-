# -i.MX6ULL-IIO-LCD-

基于正点原子 i.MX6ULL 开发板的多传感采集与 LCD 可视化终端项目。

## 项目目标
本项目实现以下功能：
- AP3216C 环境传感器接入 Linux IIO 子系统
- ICM20608 六轴惯性传感器接入 Linux IIO 子系统
- SPI LCD 显示驱动开发
- GPIO 按键中断与页面切换
- 用户态 monitor_app 实现多页面数据显示

## 目录结构
- `kernel_drivers/`：内核驱动模块
- `user_app/`：用户态应用程序
- `dts/`：设备树相关文件
- `scripts/`：构建与部署脚本
- `docs/`：项目设计与开发文档

## 构建方式
### 内核模块
```bash
cd kernel_drivers
make