# i.MX6ULL LCD 时序配置 — 7寸 1024x600 (ATK-MD0700R-1024600)

## 设备树配置

文件：`arch/arm/boot/dts/imx6ull-alientek-emmc.dts`

```dts
&lcdif {
    pinctrl-names = "default";
    pinctrl-0 = <&pinctrl_lcdif_dat
                 &pinctrl_lcdif_ctrl>;
    display = <&display0>;
    status = "okay";

    display0: display {
        bits-per-pixel = <32>;     /* 32bpp，RGB888 */
        bus-width = <24>;          /* LCD 数据线宽度 */

        display-timings {
            native-mode = <&timing0>;
            timing0: timing0 {
                clock-frequency = <51200000>;  /* 51.2 MHz */
                hactive = <1024>;              /* 水平有效像素 */
                vactive = <600>;               /* 垂直有效像素 */
                hfront-porch = <160>;
                hback-porch  = <140>;
                hsync-len    = <20>;
                vback-porch  = <20>;
                vfront-porch = <12>;
                vsync-len    = <3>;

                hsync-active  = <0>;   /* 低有效 */
                vsync-active  = <0>;   /* 低有效 */
                de-active     = <1>;   /* 高有效 */
                pixelclk-active = <0>; /* 下降沿采样 */
            };
        };
    };
};
```

## 关键参数说明

| 参数 | 7寸 1024x600 | 4.3寸 480x272 |
|------|-------------|--------------|
| clock-frequency | 51200000 | 9200000 |
| hactive | 1024 | 480 |
| vactive | 600 | 272 |
| bits-per-pixel | 32 | 16 或 32 |

## 注意事项

1. **pinctrl 冲突**：若 LCD RST 引脚被其他设备占用（如 GT911），需从 `pinctrl-0` 中移除对应 pinctrl 节点
2. **framebuffer 颜色格式**：32bpp 时默认 pixel format 为 BGR888（red.offset=16, green.offset=8, blue.offset=0），应用层构造像素值时需按 `fb_var_screeninfo` 中实际偏移量计算，避免左移负数导致未定义行为
3. **编译部署**：修改 dts 后需 `make dtbs`，替换板子上 dtb 文件，重启生效
