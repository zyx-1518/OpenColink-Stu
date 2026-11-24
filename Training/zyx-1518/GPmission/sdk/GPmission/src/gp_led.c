//=============================================================================
// gp_led.c -- GPmission LED 驱动实现（3 个 AXI GPIO 方案）
//-----------------------------------------------------------------------------
// 硬件：GPmission.v + axi_gpio_enable/mode/duty，LED 接 W13
//
// 【本文件的三个关键点，验收时都可以讲】
//
// ① TRI 寄存器必须先清零
//    AXI GPIO 复位后 gpio_io_o 是高阻（三态），必须写 GPIO_TRI=0
//    才切换到输出模式。漏这一步的典型现象是"代码逻辑全对，灯就是不亮"。
//
// ② duty 越界会让 LED 恒亮而不是关闭
//    GPmission.v: pwm_level = (duty==1)? P/4 : (duty==2)? P/2 :
//                              (duty==3)? 3P/4 : 0
//                 led <= ~(pwm_count < pwm_level)
//    duty=0 或 >3 时 pwm_level=0，(pwm_count < 0) 恒假，led 恒为 1。
//    实测确认 duty=0/4/255 时 led 高电平占 100%。
//    驱动层强制收敛到 1..3，绝不透传非法值。
//
// ③ 没有状态回读，自检只能到"寄存器级"
//    GPmission.v 没有状态输出端口，3 个 GPIO 又全是输出方向，
//    所以 STATUS 回读这件事在本方案里做不到。
//    gp_led_selftest() 验证的是"GPIO 可写可读回"，不是"LED 在闪"。
//    这点必须如实说明，不能吹成闭环自检。
//=============================================================================
#include "gp_led.h"

// ---------------------------------------------------------------------------
// 硬件访问层
// ---------------------------------------------------------------------------
#ifdef GP_LED_SIM
// PC 单测：转到 verify/sim_regs.c 的软件模拟 GPIO
// 接口带 base 参数，这样才能区分三个不同的 GPIO 实例
extern uint32_t sim_regs_read(uint32_t base, uint32_t offs);
extern void     sim_regs_write(uint32_t base, uint32_t offs, uint32_t val);

static inline void gpio_write(uint32_t base, uint32_t offs, uint32_t val)
{
    sim_regs_write(base, offs, val);
}

static inline uint32_t gpio_read(uint32_t base, uint32_t offs)
{
    return sim_regs_read(base, offs);
}

#else
// 上板：volatile 指针直连 AXI GPIO
static volatile uint32_t *g_enable = (volatile uint32_t *)GP_GPIO_ENABLE_BASE;
static volatile uint32_t *g_mode   = (volatile uint32_t *)GP_GPIO_MODE_BASE;
static volatile uint32_t *g_duty   = (volatile uint32_t *)GP_GPIO_DUTY_BASE;

static inline void gpio_write(uint32_t base, uint32_t offs, uint32_t val)
{
    volatile uint32_t *p = (volatile uint32_t *)base;
    p[offs >> 2] = val;
    (void)p[offs >> 2];              // 读回一次，等 AXI posted write 真正完成
}

static inline uint32_t gpio_read(uint32_t base, uint32_t offs)
{
    return ((volatile uint32_t *)base)[offs >> 2];
}
#endif

// 当前使用的三个基地址（上板时用宏默认值，PC 单测时由 set_base 注入）
static uint32_t s_en_base   = GP_GPIO_ENABLE_BASE;
static uint32_t s_mode_base = GP_GPIO_MODE_BASE;
static uint32_t s_duty_base = GP_GPIO_DUTY_BASE;

void gp_led_set_base(uintptr_t en, uintptr_t mode, uintptr_t duty)
{
    s_en_base   = (uint32_t)en;
    s_mode_base = (uint32_t)mode;
    s_duty_base = (uint32_t)duty;
#ifndef GP_LED_SIM
    g_enable = (volatile uint32_t *)en;
    g_mode   = (volatile uint32_t *)mode;
    g_duty   = (volatile uint32_t *)duty;
#endif
}

// ---------------------------------------------------------------------------
// 内部：写 CTRL（enable + mode 分属两个 GPIO）
// ---------------------------------------------------------------------------
static void write_enable_mode(int enable, uint32_t mode)
{
    gpio_write(s_en_base,   GP_GPIO_DATA, (enable ? 1u : 0u) & 0x1u);
    gpio_write(s_mode_base, GP_GPIO_DATA, mode & 0x3u);
}

// ---------------------------------------------------------------------------
// 内部：亮度等级(1..3) -> 要写给硬件的 duty
//   等级 3 = 最亮。
//   active-low 板子：duty 越大 -> 引脚低电平越久 -> 越亮，等级与 duty 同向
//   active-high 板子：需要反过来
// ---------------------------------------------------------------------------
static uint32_t rank_to_duty(unsigned rank)
{
    if (rank < 1u) rank = 1u;
    if (rank > 3u) rank = 3u;

#if GP_LED_ACTIVE_LOW
    return (uint32_t)rank;
#else
    return (uint32_t)(4u - rank);
#endif
}

// ---------------------------------------------------------------------------
// 初始化 —— 本文件最重要的一段
// ---------------------------------------------------------------------------
int gp_led_init(void)
{
    // ★ 第一步：三个 GPIO 全部切成输出模式
    //   TRI 寄存器每 bit 对应一个引脚：1 = 输入(高阻)，0 = 输出
    //   我们全写 0，表示所有引脚都是输出。
    gpio_write(s_en_base,   GP_GPIO_TRI, 0x0u);
    gpio_write(s_mode_base, GP_GPIO_TRI, 0x0u);
    gpio_write(s_duty_base, GP_GPIO_TRI, 0x0u);

    // ★ 第二步：给一个安全的初始状态（先关灯，再设中等亮度）
    //   顺序不能反：如果先切 PWM 模式再写 duty，
    //   切换瞬间 duty 还是旧值，LED 会先闪一下旧亮度。
    gpio_write(s_duty_base,   GP_GPIO_DATA, GP_DUTY_MED);
    gpio_write(s_en_base,     GP_GPIO_DATA, 0u);          // enable = 0，强制关灯
    gpio_write(s_mode_base,   GP_GPIO_DATA, GP_MODE_OFF);

    return 0;
}

// ---------------------------------------------------------------------------
// 基本动作
// ---------------------------------------------------------------------------
void gp_led_off(void)
{
    // enable=0 是 GPmission.v 的强制关灯路径，优先级高于 mode
    write_enable_mode(0, GP_MODE_OFF);
}

int gp_led_blink(int period_sec)
{
    uint32_t mode;

    switch (period_sec) {
        case 1:  mode = GP_MODE_1S; break;
        case 2:  mode = GP_MODE_2S; break;
        default: return -1;          // GPmission.v 只实现 1s / 2s
    }

    write_enable_mode(1, mode);
    return 0;
}

int gp_led_pwm(unsigned level)
{
    // duty=0 会让 LED 恒亮而不是关闭，绝不能透传
    if (level < GP_DUTY_MIN || level > GP_DUTY_MAX) {
        return -1;
    }
    gpio_write(s_duty_base, GP_GPIO_DATA, level & 0xFFu);
    write_enable_mode(1, GP_MODE_PWM);
    return 0;
}

void gp_led_set_brightness_rank(unsigned rank)
{
    if (rank < 1u) rank = 1u;
    if (rank > 3u) rank = 3u;

    gpio_write(s_duty_base, GP_GPIO_DATA, rank_to_duty(rank));
    write_enable_mode(1, GP_MODE_PWM);
}

// ---------------------------------------------------------------------------
// 读回（读的是 GPIO_DATA，即最后一次写入的值；
//       不代表 LED 的真实状态，因为引脚没有回读通路）
// ---------------------------------------------------------------------------
uint32_t gp_led_read_enable(void) { return gpio_read(s_en_base,   GP_GPIO_DATA) & 0x1u; }
uint32_t gp_led_read_mode(void)   { return gpio_read(s_mode_base, GP_GPIO_DATA) & 0x3u; }
uint32_t gp_led_read_duty(void)   { return gpio_read(s_duty_base, GP_GPIO_DATA) & 0xFFu; }

const char *gp_led_mode_name(uint32_t mode)
{
    switch (mode) {
        case GP_MODE_OFF: return "OFF";
        case GP_MODE_1S:  return "BLINK_1S";
        case GP_MODE_2S:  return "BLINK_2S";
        case GP_MODE_PWM: return "PWM";
        default:          return "UNKNOWN";
    }
}

// ---------------------------------------------------------------------------
// 自检
//   能验证：三个 GPIO 的 DATA 寄存器可写可读回、TRI 能正确切到输出
//   不能验证：LED 是否真的在按预期闪烁（引脚无回读通路）
//   返回位图：0x01=enable 0x02=mode 0x04=duty 0x08=TRI 设置失败
// ---------------------------------------------------------------------------
int gp_led_selftest(void)
{
    int rc = 0;
    uint32_t v;

    // 1) enable GPIO
    gpio_write(s_en_base, GP_GPIO_DATA, 1u);
    for (volatile int i = 0; i < 1000; i++) { }
    v = gpio_read(s_en_base, GP_GPIO_DATA) & 0x1u;
    if (v != 1u) rc |= 0x01;

    // 2) mode GPIO（2 bit，测一个非零值）
    gpio_write(s_mode_base, GP_GPIO_DATA, GP_MODE_2S);
    for (volatile int i = 0; i < 1000; i++) { }
    v = gpio_read(s_mode_base, GP_GPIO_DATA) & 0x3u;
    if (v != GP_MODE_2S) rc |= 0x02;

    // 3) duty GPIO（8 bit，用 0xA5 这种能测出位错的值）
    gpio_write(s_duty_base, GP_GPIO_DATA, 0xA5u);
    for (volatile int i = 0; i < 1000; i++) { }
    v = gpio_read(s_duty_base, GP_GPIO_DATA) & 0xFFu;
    if (v != 0xA5u) rc |= 0x04;

    // 4) TRI 是否真的是输出模式（读回应为 0）
    v = gpio_read(s_en_base, GP_GPIO_TRI);
    if (v != 0u) rc |= 0x08;

    // 收尾：回到关灯状态
    gp_led_off();
    return rc;
}
