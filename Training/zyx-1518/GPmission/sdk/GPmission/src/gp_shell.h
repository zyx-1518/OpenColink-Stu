//=============================================================================
// gp_shell.h -- 复用 pg002_shell.c 内核的薄封装头文件
//-----------------------------------------------------------------------------
// 把 01_shell/pg002_shell.h 的接口再导出一次，
// 并声明 shell_cmds.c 提供的命令表初始化函数。
// 编译时把 pg002_shell.c 一起加进来。
//=============================================================================
#ifndef GP_SHELL_H
#define GP_SHELL_H

#include "pg002_shell.h"

void               shell_init_led(shell_putc_fn putc_fn, shell_getc_fn getc_fn);
int                shell_cmds_count(void);
const shell_cmd_t *shell_cmds_table(void);

#endif // GP_SHELL_H
