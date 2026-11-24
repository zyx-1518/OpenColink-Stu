//=============================================================================
// pg002_shell.h  --  极简串口命令行 Shell（PG002 任务 1.2.1 选项2 风格）
//-----------------------------------------------------------------------------
// 为什么不用完整的 LetterShell：
//   * 本任务只需要"解析一行命令 + 调驱动"，用不着 history / 自动补全；
//   * 不依赖任何 BSP，PC 上能直接跑单元测试（见 05_verify/tb_shell.c）；
//   * 上板时只要把 shell_putc/shell_getc 换成 XUartPs 的收发即可。
//
// 设计要点：
//   * 一个命令表（名字 + 帮助 + 回调），线性查找，命令数 < 20 完全够快；
//   * 行缓冲固定大小，超长自动丢弃并报错，不会溢出；
//   * 只在收到 '\r' 或 '\n' 时执行，执行完回显提示符。
//   * 所有 I/O 通过函数指针注入 —— 这是它能在 PC 上单测的关键。
//=============================================================================
#ifndef PG002_SHELL_H
#define PG002_SHELL_H

#include <stdint.h>

#define SHELL_LINE_MAX      64
#define SHELL_MAX_ARGS      8
#define SHELL_MAX_CMDS      16

// 命令回调：argc/argv 已按空格切好
typedef int (*shell_cmd_fn)(int argc, char **argv);

typedef struct {
    const char   *name;
    const char   *help;
    shell_cmd_fn  fn;
} shell_cmd_t;

// I/O 注入
typedef void (*shell_putc_fn)(char c);
typedef int  (*shell_getc_fn)(void);      // 返回 -1 表示暂无数据

void shell_init(const shell_cmd_t *cmds, int n_cmds,
                shell_putc_fn putc_fn, shell_getc_fn getc_fn);

void shell_puts(const char *s);
void shell_printf_u32(uint32_t v);
void shell_printf_hex32(uint32_t v);      // 固定 8 位大写十六进制，不带 0x 前缀

// 喂一个字符进去；返回 1 表示这一行已执行完一条命令
int  shell_feed(char c);

// 喂一个完整字符串（PC 单测用，内部逐字符调用 shell_feed）
void shell_feed_str(const char *s);

// 主循环里调用：把 UART 里现有的字符全取出来消化掉
void shell_poll(void);

#endif // PG002_SHELL_H
