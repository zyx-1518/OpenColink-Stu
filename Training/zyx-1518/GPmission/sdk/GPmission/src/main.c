//=============================================================================
// main.c -- PS 主程序（GPmission + 3 个 AXI GPIO 方案）
//-----------------------------------------------------------------------------
// 上板流程：
//   1) 初始化 UART（XUartPs，115200 8N1）
//   2) gp_led_init()：把三个 GPIO 切成输出 + 关灯
//   3) 进入 Shell 主循环
//
// 编译宏：
//   -DPG002_ON_TARGET   -> 走真实 XUartPs（上板必须加）
//   不加               -> 走 stdio（PC 上测试用）
//
// 【两路线通用】
//   本文件同时支持 PG002 自定义 IP 和 GPmission+AXI GPIO，靠宏切换：
//     默认              -> pg002_led.c
//     -DGP_LED_ROUTE    -> gp_led.c（本方案）
//=============================================================================
#include <stdint.h>

#include "pg002_shell.h"

#ifdef GP_LED_ROUTE
    #include "gp_led.h"
    #define LED_DRV_INIT        gp_led_init
    #define LED_ROUTE_NAME      "GPmission + AXI GPIO"
#else
    #include "pg002_led.h"
    #define LED_DRV_INIT        pg002_led_init
    #define LED_ROUTE_NAME      "PG002 自定义 IP"
#endif

extern void              shell_init_led(shell_putc_fn putc_fn, shell_getc_fn getc_fn);
extern void              shell_banner(const char *ver);

// ---------------------------------------------------------------------------
// 硬件相关：上板走 XUartPs，PC 走 stdio
// ---------------------------------------------------------------------------
#ifdef PG002_ON_TARGET

#include "xparameters.h"
#include "xuartps.h"
#include "xil_printf.h"

static XUartPs s_uart;

static void hw_uart_init(void)
{
    XUartPs_Config *cfg = XUartPs_LookupConfig(XPAR_XUARTPS_0_BASEADDR);
    XUartPs_CfgInitialize(&s_uart, cfg, cfg->BaseAddress);
    XUartPs_SetBaudRate(&s_uart, 115200);
}

static void hw_uart_putc(char c)
{
    XUartPs_SendByte(XPAR_XUARTPS_0_BASEADDR, (uint8_t)c);
}

static int hw_uart_getc(void)
{
    if (XUartPs_IsReceiveData(XPAR_XUARTPS_0_BASEADDR)) {
        return (int)XUartPs_RecvByte(XPAR_XUARTPS_0_BASEADDR);
    }
    return -1;
}

#else

#include <stdio.h>

static void hw_uart_init(void) { }

static void hw_uart_putc(char c)
{
    putchar(c);
    fflush(stdout);
}

static int hw_uart_getc(void)
{
    int c = getchar();
    return (c == EOF) ? -1 : c;
}

#endif

// ---------------------------------------------------------------------------
int main(void)
{
    int rc;

#ifndef PG002_ON_TARGET
    setvbuf(stdout, 0, _IONBF, 0);
#endif

    hw_uart_init();

    shell_init_led(hw_uart_putc, hw_uart_getc);
    shell_banner("v1.0");

    shell_puts("路线: ");
    shell_puts(LED_ROUTE_NAME);
    shell_puts("\r\n");

    rc = LED_DRV_INIT();
    if (rc != 0) {
        shell_puts("[警告] LED 驱动初始化失败\r\n");
    } else {
        shell_puts("[OK] LED 已就绪（GPIO 已设为输出模式）\r\n");
        shell_puts("     若灯不亮：先确认 GP_LED_ACTIVE_LOW 是否设对\r\n");
    }

#ifdef PG002_ON_TARGET
    for (;;) {
        shell_poll();
    }
#else
    for (;;) {
        shell_poll();
        if (feof(stdin)) break;
    }
#endif

    return 0;
}
