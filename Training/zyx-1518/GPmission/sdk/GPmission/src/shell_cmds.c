//=============================================================================
// shell_cmds.c -- GPmission LED 的串口命令（复用 pg002_shell 内核）
//-----------------------------------------------------------------------------
// 命令：
//   led off                 关闭
//   led on1 / on2           1s / 2s 周期闪烁
//   led pwm 1|2|3           PWM 亮度等级（0 和其他值会被拒绝）
//   led dim|med|bright      语义化亮度：一定是最暗/中/最亮
//   led status              读回当前写入的配置
//   led test                自检（寄存器级，不是 LED 波形验证）
//   auto                    自动验收：6 个动作各 10 秒
//=============================================================================
#include "gp_shell.h"
#include "gp_led.h"

static volatile uint32_t s_sink;

// ---------------------------------------------------------------------------
static int parse_u32(const char *s, uint32_t *out)
{
    uint32_t v = 0;
    int digits = 0;

    if (!s || !*s) return -1;
    while (*s >= '0' && *s <= '9') {
        v = v * 10u + (uint32_t)(*s - '0');
        s++;
        if (++digits > 6) return -1;
    }
    if (*s != '\0' || digits == 0) return -1;
    *out = v;
    return 0;
}

// ---------------------------------------------------------------------------
static int cmd_led(int argc, char **argv)
{
    uint32_t v;
    const char *sub;

    if (argc < 2) {
        shell_puts("用法: led off|on1|on2|pwm <1-3>|dim|med|bright|status|test\r\n");
        return -1;
    }
    sub = argv[1];

    // ---- off ----
    if (sub[0] == 'o' && sub[1] == 'f' && sub[2] == 'f') {
        gp_led_off();
        shell_puts("LED: 已关闭\r\n");
        return 0;
    }

    // ---- on1 / on2 ----
    if (sub[0] == 'o' && sub[1] == 'n') {
        if (sub[2] == '1') { gp_led_blink(1); shell_puts("LED: 1s 周期闪烁\r\n"); return 0; }
        if (sub[2] == '2') { gp_led_blink(2); shell_puts("LED: 2s 周期闪烁\r\n"); return 0; }
        shell_puts("错误: 只有 on1 / on2（GPmission 未实现 4s）\r\n");
        return -1;
    }

    // ---- pwm <1-3> ----
    if (sub[0] == 'p' && sub[1] == 'w') {
        if (argc < 3) {
            shell_puts("用法: led pwm 1|2|3  (1=暗 2=中 3=亮)\r\n");
            return -1;
        }
        if (parse_u32(argv[2], &v) != 0) {
            shell_puts("错误: 参数必须是数字\r\n");
            return -1;
        }
        if (gp_led_pwm(v) != 0) {
            shell_puts("错误: 只接受 1/2/3。duty=0 或其他值会让 LED 恒亮（不是关闭）\r\n");
            return -1;
        }
        shell_puts("LED: PWM 等级 ");
        shell_printf_u32(v);
        shell_puts("\r\n");
        return 0;
    }

    // ---- dim / med / bright ----
    if (sub[0] == 'd' && sub[1] == 'i' && sub[2] == 'm') {
        gp_led_set_brightness_rank(1); shell_puts("LED: 暗\r\n"); return 0;
    }
    if (sub[0] == 'm' && sub[1] == 'e' && sub[2] == 'd') {
        gp_led_set_brightness_rank(2); shell_puts("LED: 中亮\r\n"); return 0;
    }
    if (sub[0] == 'b' && sub[1] == 'r') {
        gp_led_set_brightness_rank(3); shell_puts("LED: 明亮\r\n"); return 0;
    }

    // ---- status ----
    if (sub[0] == 's' && sub[1] == 't') {
        uint32_t en = gp_led_read_enable();
        uint32_t md = gp_led_read_mode();
        uint32_t dt = gp_led_read_duty();

        shell_puts("配置回读（不代表 LED 真实状态）:\r\n");
        shell_puts("  enable  = ");
        shell_printf_u32(en);
        shell_puts("\r\n  mode    = ");
        shell_printf_u32(md);
        shell_puts(" (");
        shell_puts(gp_led_mode_name(md));
        shell_puts(")\r\n  duty    = ");
        shell_printf_u32(dt);
        shell_puts("\r\n");
        return 0;
    }

    // ---- test ----
    if (sub[0] == 't') {
        int rc = gp_led_selftest();
        if (rc == 0) {
            shell_puts("自检: 通过（3 个 GPIO 读写正常）\r\n");
            shell_puts("      注意：这只验证寄存器，不验证 LED 波形\r\n");
        } else {
            shell_puts("自检: 失败, 位图=0x");
            shell_printf_hex32((uint32_t)rc & 0xFFu);
            shell_puts("\r\n  0x01=enable 0x02=mode 0x04=duty 0x08=TRI未设输出\r\n");
        }
        return rc;
    }

    shell_puts("未知子命令, 输入 led 查看用法\r\n");
    return -1;
}

// ---------------------------------------------------------------------------
// 自动验收：6 个动作各 10 秒
// ---------------------------------------------------------------------------
static void delay_10s(void)
{
    volatile uint32_t i;
    for (i = 0; i < 100u; i++) {
        for (volatile uint32_t j = 0; j < 50000000u; j++) s_sink = j;
    }
}

static void auto_step(const char *name, uint32_t expect_mode)
{
    shell_puts("auto: ");
    shell_puts(name);
    shell_puts("  ... ");
    if (gp_led_read_mode() == expect_mode) shell_puts("配置 OK, 10s\r\n");
    else                                   shell_puts("配置异常!\r\n");
    delay_10s();
}

static int cmd_auto(int argc, char **argv)
{
    (void)argc; (void)argv;

    shell_puts("\r\n===== 自动验收开始（每项 10 秒）=====\r\n");

    gp_led_blink(1);                auto_step("1s 周期闪烁", GP_MODE_1S);
    gp_led_blink(2);                auto_step("2s 周期闪烁", GP_MODE_2S);
    gp_led_set_brightness_rank(3);  auto_step("PWM 明亮",    GP_MODE_PWM);
    gp_led_set_brightness_rank(2);  auto_step("PWM 中亮",    GP_MODE_PWM);
    gp_led_set_brightness_rank(1);  auto_step("PWM 暗",      GP_MODE_PWM);
    gp_led_off();                   auto_step("关闭",        GP_MODE_OFF);

    shell_puts("===== 自动验收结束 =====\r\n");
    return 0;
}

// ---------------------------------------------------------------------------
static const shell_cmd_t g_cmds[] = {
    { "led",  "LED: off|on1|on2|pwm 1-3|dim|med|bright|status|test", cmd_led  },
    { "auto", "自动验收: 6 个动作各 10 秒",                          cmd_auto },
};

int shell_cmds_count(void) { return (int)(sizeof(g_cmds) / sizeof(g_cmds[0])); }
const shell_cmd_t *shell_cmds_table(void) { return g_cmds; }

void shell_init_led(shell_putc_fn putc_fn, shell_getc_fn getc_fn)
{
    shell_init(g_cmds, shell_cmds_count(), putc_fn, getc_fn);
}
