# GPmission — Zynq PS+PL 串口命令行控制 LED

EBAZ4205（Zynq-7010）上的 PS-PL 协同实验：PS 侧自写轻量 Shell，串口敲命令；
命令经 **AXI GPIO** 写入控制字，PL 侧的 `GPmission` 状态机驱动板载 LED
完成 1s/2s 闪烁与三档 PWM 亮度。

## 硬件平台

| 项目 | 说明 |
| --- | --- |
| 开发板 | EBAZ4205（矿板改） |
| 芯片 | `xc7z010clg400-1`（PS: 双核 Cortex-A9 + PL: Artix-7） |
| 板载 LED | LED6 为 R+G 双色管，本实验驱动 **W13**，`LVCMOS33` |
| 串口 | PS UART1 → MIO 24/25，`115200 8N1` |
| PL 时钟 | PS `FCLK_CLK0` = 100 MHz |
| PL 复位 | `proc_sys_reset` 的 `peripheral_aresetn`（低有效） |
| 下载线 | JTAG + USB-TTL |

## 开发环境

- Vivado **2018.3**
- Xilinx SDK **2018.3**
- 目标器件 `xc7z010clg400-1`

## 任务书要求与实现对照

| 任务书条目 | 要求 | 本工程对应 |
|---|---|---|
| 1.2.1 Shell 移植 | 四选一 | 选**选项 3**，自写串口控制逻辑。`sdk/src/pg002_shell.c` |
| 1.2.2 PS 驱动 | a. 1s 闪烁 b. 2s 闪烁 c. PWM 三档亮度 | `sdk/src/gp_led.c` |
| 1.2.3 AXI-Lite 接口 + PL 逻辑 | 收配置，**状态机**实现三种动作 | BD 中 3 路 AXI GPIO + `GPmission.srcs/sources_1/new/GPmission.v` |
| 1.2.4 Shell 控制验收 | 选项1 交互验收 / 选项2 自动验收 | 两者均已实现：`led` 命令 + `auto` 命令 |

选选项 3 的理由：LetterShell 功能多但要花时间读源码移植，ELShell 要从给定工程里抠，
对本任务"控制三个 LED 动作"来说都超出需要。自写两百余行够用，逻辑还能完整讲清楚。

## 设计说明

Block Design 结构：

```
ZYNQ7 PS ──M_AXI_GP0──► ps7_0_axi_periph ──┬──► axi_gpio_enable ──gpio_io_o[0:0]──┐
     │                                      ├──► axi_gpio_mode   ──gpio_io_o[1:0]──┤
     │                                      └──► axi_gpio_duty   ──gpio_io_o[7:0]──┤
     │                                                                              ▼
     │                                                                        GPmission
     │                                                                      (状态机 + PWM)
     │                                                                              │
     └─FCLK_CLK0 / peripheral_aresetn ──────────────────────────────────────►      ▼
                                                                              led_0 ──► W13
```

三路 AXI GPIO 的地址（Vivado Address Editor 分配）：

| 实例 | 基地址 | 位宽 | 连到 |
|---|---|---|---|
| `axi_gpio_enable` | `0x4121_0000` | 1 bit | `GPmission/enable` |
| `axi_gpio_mode` | `0x4122_0000` | 2 bit | `GPmission/mode[1:0]` |
| `axi_gpio_duty` | `0x4123_0000` | 8 bit | `GPmission/duty[7:0]` |

`GPmission` 的控制字位域：

| 信号 | 位宽 | 含义 |
|---|---|---|
| `mode` | 2 | `00` 熄灭 / `01` 1s 闪烁 / `10` 2s 闪烁 / `11` PWM |
| `duty` | 8 | 亮度档位 `1` 暗 / `2` 中亮 / `3` 明亮；其它值按熄灭处理 |
| `enable` | 1 | `0` 强制关灯，优先级高于 `mode` |

PWM 用档位而不是 0~255 的占空比数值，因为任务书只要求"分 3 挡"。
用档位的好处是 PS 和 PL 对"现在是哪一档"没有歧义。代价是做不了呼吸灯那种渐变。

## 命令

| 命令 | 作用 |
|---|---|
| `help` | 列出命令 |
| `led off` | 熄灭 |
| `led blink 1` / `2` | 1s / 2s 周期闪烁 |
| `led pwm 1` / `2` / `3` | 亮度档位，0 和其他值被拒绝 |
| `led dim` / `med` / `bright` | 同上，语义化写法 |
| `led status` | 读回当前写入的配置 |
| `led test` | 自检，返回失败位图 |
| `auto` | 自动验收，6 个动作各 10 秒 |

上电应看到：

```
==============================
 PG002 LED Shell v1.0
 输入 help 查看命令
==============================
pg002> 路线: GPmission + AXI GPIO
[OK] LED 已就绪（GPIO 已设为输出模式）
```

## 目录结构

```
GPmission/
├── GPmission.xpr                                    Vivado 工程文件
├── GPmission.srcs/
│   ├── constrs_1/
│   │   └── led_pins.xdc                             * 引脚约束 W13 / LVCMOS33
│   └── sources_1/
│       ├── new/
│       │   └── GPmission.v                          * PL 侧状态机 + 分频 + PWM
│       └── bd/design_1/
│           ├── design_1.bd                          Block Design（PS7 + Interconnect + 3×AXI GPIO + GPmission）
│           ├── design_1.bxml                        BD 导出副本
│           ├── design_1_ooc.xdc                     BD 的 OOC 约束
│           ├── hdl/design_1_wrapper.v               BD 顶层 wrapper
│           ├── hw_handoff/                          design_1.hwh + design_1_bd.tcl（可重建 BD）
│           ├── ui/                                  BD 画布布局
│           └── mref/GPmission/                       Module Reference 定义（component.xml）
└── sdk/
    └── GPmission/
        ├── .cproject / .project                     Eclipse 工程配置
        └── src/
            ├── main.c                               * 应用主程序（UART 初始化 + shell 主循环 + 上电自检）
            ├── pg002_shell.c / pg002_shell.h        * 串口 Shell 内核（行缓冲、切词、命令表）
            ├── gp_led.c / gp_led.h                  * 驱动（AXI GPIO 读写、参数校验、极性换算、自检）
            ├── gp_shell.h                           * 命令层头文件
            ├── shell_cmds.c                         * led / auto 命令
            ├── lscript.ld                           Xilinx 模板链接脚本
            └── Xilinx.spec
```

> 标 `*` 的是本实验自己写的部分，其余为 Xilinx 模板或工程配置。

## 关键实现

**1. AXI GPIO 必须先写 TRI 寄存器切输出**

AXI GPIO 复位后 `gpio_io_o` 处于高阻（输入态），不写 `GPIO_TRI = 0`，
数据写进 `GPIO_DATA` 也不会出现在引脚上。这是本工程最容易踩的坑：

```c
#define GP_GPIO_DATA  0x00u   // 数据寄存器
#define GP_GPIO_TRI   0x04u   // 三态控制：1=输入  0=输出

gpio_write(s_en_base,   GP_GPIO_TRI, 0x0u);   // 三个都要设
gpio_write(s_mode_base, GP_GPIO_TRI, 0x0u);
gpio_write(s_duty_base, GP_GPIO_TRI, 0x0u);
```

**2. 写寄存器后读回一次，等 AXI posted write 真正完成**

```c
static inline void gpio_write(uint32_t base, uint32_t offs, uint32_t val)
{
    volatile uint32_t *p = (volatile uint32_t *)base;
    p[offs >> 2] = val;
    (void)p[offs >> 2];      // 读回，确保写事务落地
}
```

**3. `duty` 的合法范围在 RTL 和驱动两处都收口**
- RTL：`duty` 越界按熄灭处理，不会恒亮
- 驱动：直接拒绝返回 `-1`，绝不透传非法值

**4. 极性按板卡做归一**，保证 `led bright` 一定是最亮：

```c
#if GP_LED_ACTIVE_LOW
    return (uint32_t)rank;           // 低电平点亮：duty 越大越亮
#else
    return (uint32_t)(4u - rank);    // 高电平点亮：反向
#endif
```

## 从零构建

本工程已包含完整的 `.xpr`、BD 和 SDK 工程，**正常流程是直接打开 `GPmission.xpr`**。
若要在一台新机器上重建，按下面步骤：

1. 打开 `GPmission.xpr`（Vivado 2018.3）
2. 若提示路径变化，选择更新为新位置
3. 右键 `design_1.bd` → **Generate Output Products** → **Create HDL Wrapper**（覆盖）
4. **Run Synthesis** → **Run Implementation** → **Generate Bitstream**
5. **Export Hardware**，勾选 Include bitstream
6. SDK 中导入 `sdk/GPmission` 工程（File → Import → Existing Projects into Workspace）
7. 若 BSP 缺失，右键工程 → **Re-generate BSP Sources**
8. **Program FPGA**（先下 bit）→ **Run As → Launch on Hardware**（再下 elf）
9. 串口终端 `115200 8N1`，应看到欢迎信息和 `pg002>` 提示符，再敲 `help`

## 验证结果

| 项目 | 结果 |
|---|---|
| Vivado 综合 | 通过，无 error |
| Vivado 实现 | 通过，WNS = -0.582 ns（违例在 Xilinx AXI Interconnect 内部，不涉及自定义逻辑） |
| Bitstream 生成 | 通过，`design_1_wrapper.bit` |
| DRC 检查 | 通过，无 Critical Warning |
| SDK 编译 | 通过，0 error |
| 上板实物 | **通过，W13 正常闪烁与调亮度** |

上板实测：

| 命令 | 现象 |
|---|---|
| `led blink 1` | 1 秒周期闪烁 |
| `led blink 2` | 2 秒周期闪烁 |
| `led dim` / `med` / `bright` | 三档亮度可辨 |
| `auto` | 6 个动作依次执行 10 秒 |

## 未提交的内容（均可重新生成）

`*.cache/`、`*.runs/`、`*.sim/`、`*.hw/`、`*.ip_user_files/`、BD 下的 `ip/`、`ipshared/`、`synth/`、`sim/`，
以及 SDK 的 `*_bsp/`、`*_hw_platform_*/`、`Debug/`、`webtalk/`、`*.hdf`、`*.bit`。
这些都是 Vivado/SDK 的中间产物，按上面「从零构建」的步骤可以完整重建。

## 已知问题

1. **`auto` 命令的 10 秒延时是软件空循环**，按 100MHz 估算。
   上板若发现时长偏差，换成 `sleep(10)` 或 `XTime_WaitUs(10000000)`。

2. **三路 AXI GPIO 方案没有 STATUS 回读通路**。`led status` 读回的是写入
   `GPIO_DATA` 的锁存值，不代表 LED 的真实状态。要拿到状态机当前状态，
   需把 PL 侧改成自定义 AXI 从机并加 STATUS 寄存器。

3. **`led test` 验证的是寄存器级可读写**，不是 LED 波形。闪烁周期和占空比的
   正确性通过 RTL 仿真确认。

## 许可

MIT
