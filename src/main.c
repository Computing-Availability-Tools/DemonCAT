#include "core/cli.h"
#include "core/config.h"
#include "core/registry.h"
#include "core/state.h"
#include "core/safety.h"
#include "core/output.h"
#include "core/dispatch.h"
#include "core/executor.h"

#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static void copystr_safe(char *dst, size_t cap, const char *src) {
    if (!dst || !src || cap == 0) return;
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

/* Fixed default config path, resolved relative to the dcat binary itself:
 *   <binary_dir>/../config/demoncat.conf
 * So if dcat lives at /opt/dcat/build/dcat, config is at
 * /opt/dcat/config/demoncat.conf. No setup, no --config needed; the catalog
 * and scripts always travel with the binary. --config remains as an override
 * for tests/special cases. */
static const char *default_config_path(void) {
    static char path[512];
    char exe[256];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n <= 0) return NULL;
    exe[n] = '\0';
    /* dirname() may modify its argument in place; operate on a copy. */
    char exe_copy[256];
    copystr_safe(exe_copy, sizeof exe_copy, exe);
    char *dir = dirname(exe_copy);
    snprintf(path, sizeof path, "%s/../config/demoncat.conf", dir);
    return path;
}

static void expand_state_path(const char *in, char *out, size_t cap) {
    if (in[0] == '~') {
        const char *home = getenv("HOME");
        snprintf(out, cap, "%s%s", home ? home : "", in + 1);
    } else {
        strncpy(out, in, cap - 1);
        out[cap - 1] = '\0';
    }
}

int main(int argc, char **argv) {
    const char *cfgpath = NULL;
    const char *cmdarg = NULL;
    int yes = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--config") && i + 1 < argc) cfgpath = argv[++i];
        else if (!strcmp(argv[i], "--yes") || !strcmp(argv[i], "-y")) yes = 1;
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            printf("usage: dcat \"<command> <uid> [params]\" [--config <path>] [--yes]\n");
            return 0;
        } else cmdarg = argv[i];
    }
    if (!cmdarg) {
        fprintf(stderr, "usage: dcat \"<command> <uid> [params]\"\n");
        return DCAT_E_PARSE;
    }

    if (!cfgpath) cfgpath = default_config_path();
    config_t cfg;
    if (!cfgpath || config_load(cfgpath, &cfg)) {
        output_print(result_err("", "", DCAT_E_RUN, "config not found"));
        return DCAT_E_RUN;
    }
    registry_init(&cfg);

    char sp[256];
    expand_state_path(cfg.state_file[0] ? cfg.state_file : "~/.demoncat/state.json", sp, sizeof sp);
    state_init(sp);
    state_load();
    state_set_clean_cb(dispatch_clean_record);
    state_lazy_clean();
    state_auto_clean_start();

    parsed_cmd_t pc;
    if (cli_parse(cmdarg, &pc)) {
        result_t *r = result_err("", "", DCAT_E_PARSE, "parse error");
        output_print(r);
        result_free(r);
        return DCAT_E_PARSE;
    }

    result_t *res = NULL;
    if (!strcmp(pc.op, "list")) {
        res = dispatch_list();
    } else if (!strcmp(pc.op, "query")) {
        res = dispatch_query(pc.uid, &pc.params);
    } else if (!strcmp(pc.op, "inject")) {
        const fault_def_t *f = registry_find(pc.uid);
        if (!f) {
            res = result_err("inject", pc.uid, DCAT_E_NOTFOUND, "uid not found");
        } else {
            /* C1: --yes skips WARNING-level confirmation only.
             * DANGEROUS always requires interactive "yes", even with --yes.
             * (SPEC §4.1: "--yes 不跳过 dangerous") */
            int confirmed = 0;
            if (f->safety == SAFETY_NORMAL) {
                confirmed = 1;
            } else if (yes && f->safety == SAFETY_WARNING) {
                confirmed = 1;
            }
            if (!confirmed && f->safety != SAFETY_NORMAL) {
                char buf[64];
                fprintf(stderr, "[confirm] %s (%s) - proceed? ", f->uid, f->desc);
                fflush(stderr);
                if (fgets(buf, sizeof buf, stdin)) {
                    buf[strcspn(buf, "\n")] = '\0';
                    confirmed = safety_confirm(f->safety, buf);
                } else {
                    confirmed = 0;
                }
            }
            if (!confirmed) {
                res = result_err("inject", pc.uid, DCAT_E_SAFETY, "aborted");
            } else {
                res = dispatch_inject(f, &pc.params);
                /* Auto-recovery reaper: if duration was provided, spawn a detached
                 * subprocess that sleeps then invokes 'dcat clean' at expiry, so the
                 * fault auto-recovers WITHOUT requiring another dcat invocation.
                 * (dcat is one-shot CLI; the in-process auto-clean thread dies on exit,
                 * so without this reaper the fault would persist past duration until
                 * the user runs another dcat command.) */
                if (res && res->code == 0) {
                    int dur = -1;
                    for (int i = 0; i < pc.params.count; i++) {
                        if (!strcmp(pc.params.items[i].key, "duration")) {
                            dur = atoi(pc.params.items[i].value);
                            break;
                        }
                    }
                    if (dur > 0) {
                        char exe[256] = {0};
                        ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
                        if (n <= 0) strncpy(exe, argv[0], sizeof(exe) - 1);
                        char reaper[512];
                        dispatch_build_reaper(exe, pc.uid, cfgpath, dur, reaper, sizeof reaper);
                        /* C9: check spawn result — if fork fails, auto-recovery
                         * won't fire; warn the user so they know to clean manually */
                        pid_t rpid = executor_spawn(reaper);
                        if (rpid < 0) {
                            fprintf(stderr, "[dcat] WARN: reaper spawn failed — fault will NOT auto-recover; run 'dcat \"clean %s\"' manually\n", pc.uid);
                        }
                    }
                }
            }
        }
    } else if (!strcmp(pc.op, "clean")) {
        const fault_def_t *f = registry_find(pc.uid);
        if (!f) {
            res = result_err("clean", pc.uid, DCAT_E_NOTFOUND, "uid not found");
        } else {
            res = dispatch_clean(f, &pc.params);
        }
    } else {
        res = result_err(pc.op, "", DCAT_E_PARSE, "unknown op");
    }

    output_print(res);
    int code = res ? res->code : 0;
    result_free(res);
    return code;
}
