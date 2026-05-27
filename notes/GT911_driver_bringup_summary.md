# GT911 触摸屏驱动移植全记录

## 硬件平台

- 正点原子 i.MX6ULL ALPHA V2.4 底板
- 7寸 1024x600 RGB LCD（ATK-MD0700R-1024600）
- GT911 电容触摸控制器（I2C2，地址 0x5D）
- Linux 4.1.15 内核

## 驱动架构概览

GT911 驱动属于 Linux **input 子系统**，与之前项目中 AP3216C 使用的 **IIO 子系统**不同。两者核心区别：

| | IIO（AP3216C） | input（GT911） |
|---|---|---|
| 用途 | 传感器数据采集 | 人机交互输入 |
| 用户空间接口 | `/sys/bus/iio/` sysfs 节点 | `/dev/input/eventX` 字符设备 |
| 数据方向 | 用户主动读 | 中断被动触发 |
| 数据类型 | 原始 ADC 值 + scale | 结构化 `input_event` |

GT911 工作流程：
1. 上电复位 → GT911 初始化完成 → INT 拉低通知主机
2. 主机申请下降沿中断
3. 有触摸 → GT911 拉低 INT → CPU 进入中断
4. 中断内 I2C 读取状态寄存器 + 触摸坐标 → 上报 input 子系统
5. 写状态寄存器 0x00 清除 → INT 释放回高电平
6. 下一次触摸重复第 3 步

---

## 一、驱动开发过程

### 1.1 I2C 通信层

GT911 的寄存器地址是 **16 位大端序**，这决定了不能简单使用 `i2c_smbus_read_byte_data()`（它只支持 8 位寄存器地址），必须用 `i2c_transfer()` 构造两条 I2C 消息：

```
读操作：
  消息1: 写 2 字节寄存器地址 [高字节, 低字节]
  消息2: 读 N 字节数据

写操作：
  消息1: 写 2 字节地址 + N 字节数据，合并为一个包
```

写操作中需要 `kmalloc(2 + len)` 然后 `kfree`——这里踩了一个坑：kfree 放在 `i2c_transfer` **之后**，不能放在之前，因为 `msg.buf` 指针指向这块内存，transfer 期间内核要读取它。过早释放导致 use-after-free。

### 1.2 设备树与 GPIO

GT911 需要两个 GPIO：
- **RST**（GPIO5_IO09）：控制芯片复位
- **INT**（GPIO1_IO09）：接收触摸中断通知

驱动使用新版 GPIO descriptor API：`devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW)` 会自动匹配设备树中的 `reset-gpios` 属性。这比旧版 `of_get_named_gpio()` + `gpio_request()` + `gpio_direction_output()` 三步合一要简洁得多。

### 1.3 复位时序

GT911 的复位不是简单的拉低再拉高，INT 引脚在复位过程中的电平直接决定了芯片的 **I2C 地址**：

```
RST 释放瞬间 INT 为高 → I2C 地址 0x5D
RST 释放瞬间 INT 为低 → I2C 地址 0x14
```

因此复位序列必须是：

```
1. RST 拉低（芯片进入复位）
2. INT 设为想要的物理电平（选地址）
3. RST 拉高（芯片退出复位，采样 INT）→ 地址确定
4. INT 切回输入（释放控制权，等待中断）
5. 等待 100ms（GT911 固件初始化）
```

代码中全部使用 `gpiod_direction_output_raw()` 而非 `gpiod_set_value()`，因为 raw API 直接控制物理电平，不受设备树中 `GPIO_ACTIVE_LOW`/`GPIO_ACTIVE_HIGH` 标志的影响，调试阶段更容易推理。

---

## 二、问题排查：一场逐步逼近根因的调试

驱动的 probe 流程是链式依赖的：**每一步都以前一步成功为前提**。排查必须从链条起点开始，不能跳跃。

### 第一个问题：I2C 读 PID 返回 -EIO

加载驱动后日志：

```
gt911 1-005d: failed to read product ID: -5
```

`-5` 即 `-EIO`。PID 寄存器都读不到，意味着 GT911 根本没有响应 I2C。

排查思路：先确认芯片是否活着。GT911 不响应 I2C 的可能原因很多——芯片一直处于复位、I2C 地址不对、总线不通、pinctrl 没配等等。需要在复位函数中加电平监控，找出最可能的那个。

### 第二个问题：RST 引脚始终为物理低电平

在 `gt911_reset()` 中添加 raw 电平打印：

```
before release: rst raw=0    ← assert RST，预期 0，正常
after release:  rst raw=0    ← 释放 RST 后应该变成 1，但仍是 0！
after int in:   rst raw=0    ← 始终为 0
```

RST 引脚无论怎么操作都是物理低电平。这意味着 GT911 **一直处于复位状态**——芯片根本没在工作，I2C 通信失败是必然结果。

**排查 RST 为什么拉不高：**

第一步，排除是 gpiod API 的问题。临时在 probe 中插入旧 GPIO API 测试：

```c
gpio_request(137, "test");        // GPIO5_IO09 = gpio 137
gpio_direction_output(137, 0);    // 设低
gpio_direction_output(137, 1);    // 设高
```

结果 `gpio_get_value(137)` 始终为 0——即使用最底层的 API 也拉不高。**问题不在 gpiod API，在更底层。**

第二步，检查 pinctrl 是否生效：

```bash
cat /sys/kernel/debug/pinctrl/*/pinmux-pins | grep TAMPER9
# 输出: (MUX UNCLAIMED) (GPIO UNCLAIMED)
```

**MUX UNCLAIMED 意味着引脚复用从未被配置**。pinctrl 根本没生效。

### 第三个问题：pinctrl 为什么不生效

GPIO5_IO09 属于 **SNVS 域**，它的引脚复用必须在 `&iomuxc_snvs` 节点下配置，而不是普通的 `&iomuxc`。两个 pin controller 是独立的硬件模块：

| Pin Controller | 地址 | 管辖的引脚 |
|----------------|------|-----------|
| `&iomuxc` | 0x020E0000 | GPIO1 ~ GPIO4 |
| `&iomuxc_snvs` | 0x02290000 | GPIO5 + BOOT_MODE |

最初把 RST 和 INT 两个引脚都放在同一个 `pinctrl_gt911` 节点下，而这个节点在 `&iomuxc` 中。INT (GPIO1_IO09) 属于 iomuxc 管辖，配置生效；但 RST (GPIO5_IO09) 属于 iomuxc_snvs 管辖，放在 iomuxc 下是**静默无效**的——DTC 不会报错，但 pin controller 驱动根本不认识这个引脚编号。

修复方案：

```dts
// INT → 放在 iomuxc 下
&iomuxc {
    ts_int_pin: ts_int_pin_mux {
        fsl,pins = <
            MX6UL_PAD_GPIO1_IO09__GPIO1_IO09  0x79
        >;
    };
};

// RST → 放在 iomuxc_snvs 下
&iomuxc_snvs {
    ts_reset_pin: ts_reset_pin_mux {
        fsl,pins = <
            MX6ULL_PAD_SNVS_TAMPER9__GPIO5_IO09  0x79
        >;
    };
};

// GT911 节点中引用两者的 phandle
gt911@5d {
    pinctrl-0 = <&ts_int_pin>, <&ts_reset_pin>;
};
```

这里 `pinctrl-0` 中两个 phandle 使用逗号分隔（DTC 将其解释为一个 phandle 列表），指向不同的 pin controller。内核 pinctrl 框架会根据每个 group 所属的 controller 自动路由配置。

### 第四个问题：dtb 没有真正生效

修改 DTS 后编译，但板端 `/proc/device-tree` 中仍然找不到 `gt911@5d`。

根因：板子通过 **TFTP** 加载 dtb，dtb 文件在 `/home/why/zdyz/tftpboot/`，而不是 NFS 目录。之前一直往 NFS 路径拷，板子加载的始终是旧的 dtb。

教训：修改设备树后必须确认：
```bash
find /proc/device-tree -name "gt911@5d"   # 确认节点存在
grep -r "sensor_collect,gt911" /proc/device-tree 2>/dev/null
```

### 第五个问题：GPIO1_IO09 pinctrl 冲突

修复 RST pinctrl 后，又出现新错误：

```
imx6ul-pinctrl: pin MX6UL_PAD_GPIO1_IO09 already requested by 20e0000.iomuxc;
cannot claim for 1-005d
```

原因：`ts_int_pin` 被同时放入了两个地方的 `pinctrl-0`——一个是 `&iomuxc` 的默认状态，另一个是 `gt911@5d` 节点。内核不允许同一个引脚被两个设备同时 claim。

修复：只保留 `gt911@5d` 节点中的 `pinctrl-0` 引用，从 pin controller 的 default 状态中移除。

### 调试总结：从现象到根因的推理链

```
I2C 读 PID 失败 (-EIO)
  → RST 引脚始终物理低 (raw=0)
    → 旧 GPIO API 也拉不高
      → pinctrl 显示 (MUX UNCLAIMED)
        → RST pinctrl 放错了 pin controller（SNVS 域问题）
          → 修复后 dtb 未生效（部署路径错误）
            → 修复后 pinctrl 冲突（重复引用）
              → 最终修复：正确的 DTS + 正确的部署路径 → 驱动正常工作
```

每一步排查都在缩小问题范围：I2C → GPIO → pinctrl → device tree source → device tree binary → dtb deployment。这是嵌入式 Linux 驱动调试的典型路径。

---

## 三、最终工作状态

### 3.1 设备树（完整）

```dts
&i2c2 {
    gt911@5d {
        compatible = "sensor_collect,gt911";
        reg = <0x5d>;
        pinctrl-names = "default";
        pinctrl-0 = <&ts_int_pin>, <&ts_reset_pin>;
        reset-gpios = <&gpio5 9 GPIO_ACTIVE_LOW>;
        interrupt-parent = <&gpio1>;
        interrupts = <9 IRQ_TYPE_EDGE_FALLING>;
        irq-gpios = <&gpio1 9 GPIO_ACTIVE_LOW>;
        status = "okay";
    };
};

&iomuxc {
    ts_int_pin: ts_int_pin_mux {
        fsl,pins = <MX6UL_PAD_GPIO1_IO09__GPIO1_IO09  0x79>;
    };
};

&iomuxc_snvs {
    ts_reset_pin: ts_reset_pin_mux {
        fsl,pins = <MX6ULL_PAD_SNVS_TAMPER9__GPIO5_IO09  0x79>;
    };
};
```

### 3.2 正常驱动初始化流程

```
insmod gt911_ts.ko
  │
  gt911_probe()
  ├─ devm_kzalloc                 分配私有数据
  ├─ devm_gpiod_get("reset")      获取 RST (GPIO5_IO09, ACTIVE_LOW)
  ├─ devm_gpiod_get("irq")        获取 INT (GPIO1_IO09, ACTIVE_LOW)
  ├─ gt911_reset()
  │   ├─ RST raw=0                 芯片复位
  │   ├─ INT raw=0                 地址选择
  │   ├─ RST raw=1                 退出复位, 采样 INT → 0x5D
  │   ├─ INT → input               释放 INT, 等 100ms
  │   └─ I2C 读 PID                "GT911 ID: 911"
  ├─ gt911_input_register()
  │   ├─ input_allocate_device     分配 input 设备
  │   ├─ 注册 ABS_MT_POSITION_X/Y  多点触摸坐标
  │   ├─ 注册 ABS_X/Y              单点兼容
  │   ├─ 注册 BTN_TOUCH            触摸按键
  │   ├─ input_mt_init_slots(5)    5 点触摸
  │   └─ input_register_device     → /dev/input/eventX
  ├─ gpiod_to_irq                  中断号
  └─ request_threaded_irq          下降沿触发 → gt911_irq_handler
       │
       "GT911 probed"
```

### 3.3 中断处理流程

```
触摸发生
  ↓
GT911 拉低 INT → CPU 检测下降沿 → gt911_irq_handler()
  ├─ I2C 读 0x814E (GT_GSTID_REG)
  │   bit7=1 → 有数据, bit[3:0] → 触摸点数
  ├─ I2C 读 0x814F (GT_TP_REG)
  │   每点 8 字节: [id][x_lo][x_hi][y_lo][y_hi][size_lo][size_hi][reserved]
  ├─ input_mt_slot()               指定触摸点
  ├─ input_report_abs(X/Y/SIZE)    上报坐标
  ├─ input_mt_sync_frame()         帧同步
  ├─ input_sync()                  事件上报
  └─ I2C 写 0x814E = 0x00         清除状态 → INT 回高
       ↓
     等待下次触摸
```

关键点：状态清除必须写 **GT_GSTID_REG (0x814E)**，不能写成 GT_CTRL_REG (0x8040)。否则缓冲状态不会被清除，GT911 不会释放 INT，后续触摸不会再触发中断。

### 3.4 板端验证

```bash
# 加载驱动
insmod gt911_ts.ko

# 确认 input 设备
cat /proc/bus/input/devices | grep GT911
# N: Name="GT911 Touchscreen"
# H: Handlers=mouse0 event1

# 中断计数（触摸前后执行两次，数字增加说明中断正常）
cat /proc/interrupts | grep gt911

# 测试程序
./input_test /dev/input/event1
# 触摸屏幕 → ABS_MT_POSITION_X/Y → SYN_REPORT
```

### 3.5 input 设备能力

| 事件 | 范围 | 用途 |
|------|------|------|
| ABS_MT_POSITION_X | 0-1024 | 多点 X |
| ABS_MT_POSITION_Y | 0-600 | 多点 Y |
| ABS_MT_TOUCH_MAJOR | 0-255 | 触摸面积 |
| ABS_X / ABS_Y | 0-1024 / 0-600 | 单点兼容 |
| BTN_TOUCH | 0/1 | 触摸状态 |

---

## 四、经验总结

### 4.1 驱动调试方法论

1. **链式依赖原则**：probe 中各步骤有严格先后顺序。前一步失败时后面的调试没有意义。排查必须从链条最前端开始。

2. **缩小范围策略**：I2C 失败 → 检查芯片是否复位 → 检查 GPIO → 检查 pinctrl → 检查 DTS → 检查 DTB 部署。每次排除一层可能原因。

3. **对照测试法**：当怀疑 gpiod API 有问题时，用旧 GPIO API 做对照。如果两种 API 结果一致（都失败），问题在更底层。

4. **逐级日志**：在关键的硬件操作前后打印状态（`gpiod_get_raw_value`），形成时序日志，快速定位哪一步出了问题。

### 4.2 设备树注意事项

- **SNVS 引脚**必须放在 `&iomuxc_snvs` 下，放在 `&iomuxc` 下是静默无效的
- pinctrl group 不能同时被多个 `pinctrl-0` 引用
- 修改 DTS 后要确认 dtb 部署到了正确的启动路径（TFTP/NFS/eMMC）
- 启动后用 `/proc/device-tree` 验证节点是否真的存在

### 4.3 GPIO 使用建议

- 调试阶段用 `gpiod_*_raw()` API，直接控制物理电平，避免 `ACTIVE_LOW`/`ACTIVE_HIGH` 带来的逻辑翻转困惑
- `gpiod_get_optional()` 可能返回 NULL（属性不存在），需要同时检查 `IS_ERR` 和 `!ptr`
- GPIO descriptor 和旧 API（整数编号）不能混用
