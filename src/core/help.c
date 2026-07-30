#include "help.h"
#include "registry.h"
#include "precheck.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ---- 极简可增长字符串缓冲（OOM 时停止追加，避免截断） ---- */
typedef struct { char *buf; size_t len, cap; int oom; } sb_t;

static void sb_init(sb_t *s) {
    s->cap = 512; s->len = 0; s->oom = 0;
    s->buf = malloc(s->cap);
    if (!s->buf) { s->oom = 1; s->cap = 0; }
    else s->buf[0] = '\0';
}

static void sb_reserve(sb_t *s, size_t add) {
    if (s->oom) return;
    size_t need = s->len + add + 1;
    if (need <= s->cap) return;
    size_t c = s->cap ? s->cap : 16;
    while (c < need) c *= 2;
    char *nb = realloc(s->buf, c);
    if (!nb) { s->oom = 1; return; }
    s->buf = nb; s->cap = c;
}

static void sb_addf(sb_t *s, const char *fmt, ...) {
    if (s->oom) return;
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) { va_end(ap2); return; }
    sb_reserve(s, (size_t)n);
    if (!s->oom && s->len + (size_t)n + 1 <= s->cap) {
        vsnprintf(s->buf + s->len, s->cap - s->len, fmt, ap2);
        s->len += (size_t)n;
    }
    va_end(ap2);
}

static char *sb_done(sb_t *s) {
    if (s->oom) { free(s->buf); return strdup(""); }
    return s->buf;
}

/* ---- 文案 ---- */
static const char *op_usage(const char *op) {
    if (strcmp(op, "inject") == 0) return "dcat inject <uid> --<param>=<value> ...";
    if (strcmp(op, "clean")  == 0) return "dcat clean <uid> [--<param>=<value> ...]  |  dcat clean --all";
    if (strcmp(op, "query")  == 0) return "dcat query [uid] [--<param>=<value> ...]";
    if (strcmp(op, "list")   == 0) return "dcat list";
    return "dcat <subcommand> [uid] [--key=value ...]";
}

static const char *op_desc(const char *op) {
    if (strcmp(op, "inject") == 0) return "注入故障；可恢复故障写 state + 返回 record_id；inject-only 不写 state";
    if (strcmp(op, "clean")  == 0) return "清除活跃注入：clean <uid> --params 按参数匹配 state 记录逐条清理；"
                                       "clean <uid> 无参=清该 uid 全部 /tmp 工件(脚本自 glob)；"
                                       "clean --all=对所有故障 fan-out 无参 clean；均 stateless，state.json 丢失/损坏仍可清";
    if (strcmp(op, "query")  == 0) return "无 uid 列出全部活跃注入；有 uid 走脚本 query 直通 stdout + confirmed（参数可选，无参=查全部）";
    if (strcmp(op, "list")   == 0) return "列出故障目录（cnf + 动态插件）";
    return "";
}

/* 获取操作对应的 required 字段指针 */
static const char *get_op_required(const fault_def_t *f, const char *op) {
    if (strcmp(op, "inject") == 0) return f->inject_required;
    if (strcmp(op, "clean")  == 0) return f->clean_required;
    if (strcmp(op, "query")  == 0) return f->query_required;
    return "";
}
static const char *get_op_optional(const fault_def_t *f, const char *op) {
    if (strcmp(op, "inject") == 0) return f->inject_optional;
    if (strcmp(op, "clean")  == 0) return f->clean_optional;
    if (strcmp(op, "query")  == 0) return f->query_optional;
    return "";
}

/* 按 op 的 required/optional 拼参数示例 */
static void render_example(sb_t *s, const char *op, const fault_def_t *f) {
    sb_addf(s, "  示例：dcat %s %s", op, f->uid);
    char buf[128];
    const char *req = get_op_required(f, op);
    const char *opt = get_op_optional(f, op);
    if (req[0]) {
        strncpy(buf, req, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
        char *save = NULL, *tok = strtok_r(buf, ",", &save);
        while (tok) { sb_addf(s, " --%s=<%s>", tok, tok); tok = strtok_r(NULL, ",", &save); }
    }
    if (opt[0]) {
        strncpy(buf, opt, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
        char *save = NULL, *tok = strtok_r(buf, ",", &save);
        while (tok) { sb_addf(s, " [--%s=<%s>]", tok, tok); tok = strtok_r(NULL, ",", &save); }
    }
    sb_addf(s, "\n");
}

static void render_fault_table(sb_t *s, const char *op) {
    int n = 0;
    const fault_def_t *list = registry_list(&n);
    int printed = 0;
    for (int i = 0; i < n; i++) {
        if (!op_in_supported(list[i].supported_ops, op)) continue;
        const char *req = get_op_required(&list[i], op);
        const char *opt = get_op_optional(&list[i], op);
        sb_addf(s, "  %-24s %s", list[i].uid, req[0] ? req : "（无必填）");
        if (opt[0]) sb_addf(s, "  [可选: %s]", opt);
        sb_addf(s, "\n");
        printed++;
    }
    if (!printed) sb_addf(s, "  （无）\n");
}

/* ---- 对外渲染 ---- */
char *help_render_global(void) {
    sb_t s; sb_init(&s);
    sb_addf(&s,
        "usage: dcat <subcommand> [uid] [--key=value ...] [--config <path>] [--plugins <dir>] [--help]\n"
        "  subcommand: inject | clean | query | list\n"
        "  inject <uid> --p1=v1 ...     注入故障\n"
        "  clean  <uid> [--k=v ...]     清除活跃注入；无参=清该 uid 全部工件；--all=清全部故障(stateless)\n"
        "  query  [uid] [--k=v ...]     无 uid 列出活跃注入；有 uid 验证故障是否生效（参数可选，无参=查全部）\n"
        "  list                         列出故障目录\n"
        "  --config <path>              指定 demoncat.conf 路径（默认 <root>/config/demoncat.conf）\n"
        "  --plugins <dir>              指定动态插件目录（默认 <root>/plugins）\n"
        "  --all                        仅 clean：无参清理全部故障（state.json 丢失/损坏时仍可清）\n"
        "  --help                       打印本帮助；置于子命令后可显示该子命令参数\n");
    return sb_done(&s);
}

char *help_render_subcommand(const char *op, const char *uid) {
    if (!op || !op[0]) return help_render_global();

    sb_t s; sb_init(&s);
    sb_addf(&s, "usage: %s\n  %s\n", op_usage(op), op_desc(op));
    sb_addf(&s, "  --help                       打印本帮助\n");

    if (strcmp(op, "list") == 0) {
        sb_addf(&s, "  运行 `dcat list` 查看完整故障目录（含动态插件）\n");
        return sb_done(&s);
    }

    /* inject/clean/query */
    if (uid && uid[0]) {
        const fault_def_t *f = registry_find(uid);
        if (f) {
            const char *req = get_op_required(f, op);
            const char *opt = get_op_optional(f, op);
            sb_addf(&s, "\n故障 %s：%s\n", f->uid, f->desc[0] ? f->desc : "（无描述）");
            sb_addf(&s, "  支持操作：%s\n", f->supported_ops);
            sb_addf(&s, "  %s 必填参数：%s\n", op, req[0] ? req : "（无）");
            sb_addf(&s, "  %s 可选参数：%s\n", op, opt[0] ? opt : "（无）");
            render_example(&s, op, f);
        } else {
            sb_addf(&s, "\n（未知故障 uid：%s）\n", uid);
        }
    }

    sb_addf(&s, "\n支持 %s 的故障（必填参数 [可选参数]）：\n", op);
    render_fault_table(&s, op);
    sb_addf(&s, "\n动态插件故障参数见 `dcat list`\n");
    return sb_done(&s);
}

void help_print_global(void) {
    char *t = help_render_global();
    if (t) { fputs(t, stdout); free(t); }
}

void help_print_subcommand(const char *op, const char *uid) {
    char *t = help_render_subcommand(op, uid);
    if (t) { fputs(t, stdout); free(t); }
}
