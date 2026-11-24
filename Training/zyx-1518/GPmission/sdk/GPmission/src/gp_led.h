//=============================================================================
// gp_led.h -- GPmission LED 驱动 (PS 侧)
//-----------------------------------------------------------------------------
// 【本驱动适配的硬件】
//   PL 侧：GPmission.v（你自己的 LED 逻辑）+ 3 个独立 AXI GPIO
//
//   FPGA 引脚：W13（LED6 的 R+G 双色 LED 其中一路）
//
//   三个 AXI GPIO（地址来自 Vivado Address Editor，已核实）：
//     0x4121_0000  axi_gpio_enable  1 bit  -> GPmission/enable
//     0x4122_0000  axi_gpio_mode    2 bit  -> GPmission/mode
//     0x4123_0000  axi_gpio_duty    8 bit  -> GPmission/duty
//
// 【与"自定义 AXI 从机"方案的关键差异】
//   之前那套（0x43C0_0000 + led_gp_regs.v）有一个 STATUS 寄存器可以回读
//   状态机当前状态，能做真闭环自检。
//   本方案用 3 个纯输出 GPIO，GPmission.v 也没有状态输出端口，
//   所以【无法回读任何状态】——自检只能验证"写进去的值读回来一致"，
//   不能证明"LED 真的在按预期闪烁"。这一点必须在验收时讲清楚。
//
// 【最容易踩的坑：TRI 寄存器】
//   AXI GPIO 上电后默认是【输入】模式（gpio_io_o 为高阻），
//   不把三态寄存器 GPIO_TRI 写成 0，你写进 GPIO_DATA 的值根本不会输出到引脚，
//   表现为"程序没错但灯永远不亮"。gp_led_init() 里已处理。
//=============================================================================
#ifndef GP_LED_H
#define GP_LED_H

#include <stdint.h>
#include <stddef.h>

// ---------------------------------------------------------------------------
// 三个 AXI GPIO 的基地址（Vivado Address Editor 实际分配值）
// 若你以后重新分配地址，只改这三行。
// ---------------------------------------------------------------------------
#ifndef GP_GPIO_ENABLE_BASE
#define GP_GPIO_ENABLE_BASE     0x41210000u
#endif

#ifndef GP_GPIO_MODE_BASE
#define GP_GPIO_MODE_BASE       0x41220000u
#endif

#ifndef GP_GPIO_DUTY_BASE
#define GP_GPIO_DUTY_BASE       0x41230000u
#endif

// ---------------------------------------------------------------------------
// AXI GPIO 标准寄存器偏移（Xilinx pg144）
// ---------------------------------------------------------------------------
#define GP_GPIO_DATA            0x00u   // 数据寄存器：读=当前输入，写=输出值
#define GP_GPIO_TRI             0x04u   // 三态控制：1=输入(高阻)  0=输出
#define GP_GPIO2_DATA           0x08u   // 双通道 GPIO 的第二通道（本设计未用）

// ---------------------------------------------------------------------------
// mode 编码（与 GPmission.v 的 localparam 一致）
// ---------------------------------------------------------------------------
#define GP_MODE_OFF             0x0u    // 关闭
#define GP_MODE_1S              0x1u    // 1 秒周期闪烁
#define GP_MODE_2S              0x2u    // 2 秒周期闪烁
#define GP_MODE_PWM             0x3u    // PWM，亮度由 duty 决定

// ---------------------------------------------------------------------------
// duty 取值：GPmission.v 只认 1 / 2 / 3
//   1 -> PWM_PERIOD/4     2 -> PWM_PERIOD/2     3 -> 3*PWM_PERIOD/4
//   其它值 -> pwm_level = 0 -> LED 恒亮（不是关闭！）
//   驱动层会拒绝 0 和 >3，绝不把非法值写给硬件。
// ---------------------------------------------------------------------------
#define GP_DUTY_DIM             0x1u
#define GP_DUTY_MED             0x2u
#define GP_DUTY_BRIGHT          0x3u
#define GP_DUTY_MIN             0x1u
#define GP_DUTY_MAX             0x3u

// ---------------------------------------------------------------------------
// 【关键配置】LED 引脚有效电平 —— 必须按你的板子核实
//
// 实测 GPmission.v: led <= ~(pwm_count < pwm_level)
//   duty=1 -> led 引脚高电平占 75%
//   duty=3 -> led 引脚高电平占 25%
// 即 duty 越大，引脚为高的时间越短。
//
// 原理图 LED6 是 R+G 双色 LED，pin1/pin2 经 150R 电阻接公共端，
// pin3/pin4 直连 FPGA。这种接法通常是【共阳】= 低电平点亮，
// 但必须以实测为准。
//
//   1 = 低电平点亮（active-low，常见）
//   0 = 高电平点亮（active-high）
//
// 验证方法：烧进去后敲 led bright，若灯变暗或不亮，把这里改成 0。
// ---------------------------------------------------------------------------
#ifndef GP_LED_ACTIVE_LOW
#define GP_LED_ACTIVE_LOW       1
#endif

// ---------------------------------------------------------------------------
// 驱动接口
// ---------------------------------------------------------------------------
void      gp_led_set_base(uintptr_t en, uintptr_t mode, uintptr_t duty);

// 初始化：把三个 GPIO 设成输出模式 + 关灯。返回 0 成功。
// 【必须先调用】否则 GPIO 保持高阻输入态，写什么都不生效。
int       gp_led_init(void);

// 自检：验证三个 GPIO 可读可写。返回 0 通过，非 0 为失败位图。
// 只能证明"寄存器读写正常"，不能证明 LED 实际在工作。
int       gp_led_selftest(void);

// 关闭 / 闪烁
void      gp_led_off(void);
int       gp_led_blink(int period_sec);      // 只接受 1 或 2，返回 0 成功，-1 非法

// PWM 亮度：level 取 1/2/3（GP_DUTY_*），返回 0 成功，-1 非法
int       gp_led_pwm(unsigned level);

// 语义化亮度：rank 1=最暗 2=中 3=最亮，自动按 GP_LED_ACTIVE_LOW 换算
void      gp_led_set_brightness_rank(unsigned rank);

// 读回当前写入的配置（注意：这不代表 LED 的真实状态）
uint32_t  gp_led_read_enable(void);
uint32_t  gp_led_read_mode(void);
uint32_t  gp_led_read_duty(void);

const char *gp_led_mode_name(uint32_t mode);

#endif // GP_LED_H
