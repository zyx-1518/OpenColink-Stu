//=============================================================================
// pg002_shell.c  --  极简串口命令行 Shell 实现
//-----------------------------------------------------------------------------
// 代码风格遵循 PG001 实验室规范：
//   * 所有函数先声明后使用，头文件里已给出对外接口；
//   * 不使用动态内存（无 malloc），全部静态缓冲区，嵌入式友好；
//   * 输入解析自己做，不做 strtok（避免修改用户缓冲区）；
//   * 变量命名小写下划线，宏全大写。
//=============================================================================
#include "pg002_shell.h"

// ---------------------------------------------------------------------------
// 静态状态
// ---------------------------------------------------------------------------
static const shell_cmd_t *s_cmds   = 0;
static int                s_ncmds  = 0;
static shell_putc_fn      s_putc   = 0;
static shell_getc_fn      s_getc   = 0;

static char s_line[SHELL_LINE_MAX];
static int  s_len = 0;
static int  s_overflow = 0;

// ---------------------------------------------------------------------------
// 底层输出
// ---------------------------------------------------------------------------
static void put_c(char c)
{
    if (s_putc) s_putc(c);
}

void shell_puts(const char *s)
{
    if (!s) return;
    while (*s) put_c(*s++);
}

void shell_printf_u32(uint32_t v)
{
    char buf[11];
    int  i = 0;

    if (v == 0) { shell_puts("0"); return; }

    while (v > 0 && i < (int)sizeof(buf) - 1) {
        buf[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (i > 0) put_c(buf[--i]);
}

// 固定 8 位大写十六进制。Shell 里读寄存器、看位图都靠它，
// 不能用 shell_printf_u32 代替（那个只输出十进制字符）。
void shell_printf_hex32(uint32_t v)
{
    static const char kHex[] = "0123456789ABCDEF";
    int shift;

    for (shift = 28; shift >= 0; shift -= 4) {
        put_c(kHex[(v >> shift) & 0xFu]);
    }
}

// ---------------------------------------------------------------------------
// 初始化
// ---------------------------------------------------------------------------
void shell_init(const shell_cmd_t *cmds, int n_cmds,
                shell_putc_fn putc_fn, shell_getc_fn getc_fn)
{
    s_cmds   = cmds;
    s_ncmds  = n_cmds;
    s_putc   = putc_fn;
    s_getc   = getc_fn;
    s_len    = 0;
    s_overflow = 0;
}

// ---------------------------------------------------------------------------
// 字符串比较（不依赖 libc，方便裸机移植）
// ---------------------------------------------------------------------------
static int str_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return (*a == '\0' && *b == '\0');
}

// 打印提示符
static void prompt(void)
{
    shell_puts("pg002> ");
}

// ---------------------------------------------------------------------------
// 帮助
// ---------------------------------------------------------------------------
static void print_help(void)
{
    int i;
    shell_puts("\r\n可用命令:\r\n");
    for (i = 0; i < s_ncmds; i++) {
        shell_puts("  ");
        shell_puts(s_cmds[i].name);
        shell_puts("\t- ");
        shell_puts(s_cmds[i].help);
        shell_puts("\r\n");
    }
}

// ---------------------------------------------------------------------------
// 解析并执行一行
//   约定：
//     * 命令名不区分大小写
//     * 带 -h/--help 或参数个数不对时打印该命令帮助
//     * 内置命令 help 由 shell 自己处理，不占用户命令表
// ---------------------------------------------------------------------------
static void exec_line(char *line)
{
    char *argv[SHELL_MAX_ARGS];
    int   argc = 0;
    char *p = line;
    int   i;

    // ---- 切词：只按空格/Tab 切，连续空格跳过 ----
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;
        if (argc >= SHELL_MAX_ARGS) {
            shell_puts("错误: 参数过多\r\n");
            return;
        }
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = '\0';
    }

    if (argc == 0) return;

    // ---- 命令名转小写，实现大小写不敏感 ----
    for (i = 0; argv[0][i]; i++) {
        if (argv[0][i] >= 'A' && argv[0][i] <= 'Z')
            argv[0][i] = (char)(argv[0][i] - 'A' + 'a');
    }

    // ---- 内置 help ----
    if (str_eq(argv[0], "help")) {
        print_help();
        return;
    }

    // ---- 查表 ----
    for (i = 0; i < s_ncmds; i++) {
        if (str_eq(argv[0], s_cmds[i].name)) {
            // 支持 cmd -h 查该命令帮助
            if (argc >= 2 && (str_eq(argv[1], "-h") || str_eq(argv[1], "--help"))) {
                shell_puts(s_cmds[i].name);
                shell_puts(": ");
                shell_puts(s_cmds[i].help);
                shell_puts("\r\n");
                return;
            }
            (void)s_cmds[i].fn(argc, argv);
            return;
        }
    }

    shell_puts("未知命令: ");
    shell_puts(argv[0]);
    shell_puts("  (输入 help 查看全部)\r\n");
}

// ---------------------------------------------------------------------------
// 喂字符
// ---------------------------------------------------------------------------
int shell_feed(char c)
{
    // 回车 / 换行 -> 执行
    if (c == '\r' || c == '\n') {
        // 忽略紧跟的 \n（Windows 串口工具常发 \r\n）
        if (c == '\n' && s_len == 0 && !s_overflow) {
            prompt();
            return 0;
        }
        if (s_overflow) {
            shell_puts("错误: 命令行过长，已丢弃\r\n");
            s_len = 0;
            s_overflow = 0;
            prompt();
            return 0;
        }
        s_line[s_len] = '\0';
        shell_puts("\r\n");
        if (s_len > 0) exec_line(s_line);
        s_len = 0;
        prompt();
        return 1;
    }

    // 退格
    if (c == '\b' || (unsigned char)c == 0x7F) {
        if (s_len > 0) {
            s_len--;
            shell_puts("\b \b");          // 终端里擦掉一个字符
        }
        return 0;
    }

    // 其他可见字符
    if (c >= 0x20 && c < 0x7F) {
        if (s_len < SHELL_LINE_MAX - 1) {
            s_line[s_len++] = c;
            put_c(c);                     // 本地回显
        } else {
            s_overflow = 1;
        }
    }
    return 0;
}

void shell_feed_str(const char *s)
{
    while (s && *s) shell_feed(*s++);
}

void shell_poll(void)
{
    int c;
    if (!s_getc) return;
    while ((c = s_getc()) >= 0) {
        shell_feed((char)c);
    }
}

// 供主循环开始时打印 banner
void shell_banner(const char *ver)
{
    shell_puts("\r\n");
    shell_puts("==============================\r\n");
    shell_puts(" PG002 LED Shell ");
    shell_puts(ver ? ver : "");
    shell_puts("\r\n 输入 help 查看命令\r\n");
    shell_puts("==============================\r\n");
    prompt();
}
