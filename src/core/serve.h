#ifndef DCAT_SERVE_H
#define DCAT_SERVE_H
/* 启动 dcat HTTP 控制平面(长驻)。阻塞直到 SIGTERM/SIGINT。
 *   port      : 监听端口(<=0 → 默认 8080)
 *   bind_addr : 绑定地址(NULL → "127.0.0.1";明文,安全由 SSH 隧道兜底)
 *   webroot   : 静态前端根目录(NULL → 自动派生 <exe_dir>/../src/web)
 *   registry/state/plugin 须由调用方先装配(main.c 已做)。
 *   返回进程退出码(0 正常)。 */
int serve_run(int port, const char *bind_addr, const char *webroot, int allow_write);
#endif
