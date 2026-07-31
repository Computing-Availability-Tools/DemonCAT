#include "config.h"
#include "registry.h"
#include "state.h"
#include "dispatch.h"
#include "output.h"
#include "cli.h"
#include "help.h"
#include "plugins/plugin_manager.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static const char *resolve_cfgpath(const char *override, char *buf, size_t cap) {
    if (override) return override;
    ssize_t n = readlink("/proc/self/exe", buf, (int)cap - 1);
    if (n > 0) {
        buf[n] = '\0';
        char *slash = strrchr(buf, '/'); if (slash) *slash = '\0';
        char *bslash = strrchr(buf, '/'); if (bslash) *bslash = '\0';
        snprintf(buf + strlen(buf), cap - strlen(buf), "/config/demoncat.conf");
        return buf;
    }
    return "config/demoncat.conf";
}

int main(int argc, char **argv) {
    parsed_cmd_t pc;
    int rc = cli_parse(argc, argv, &pc);

    /* --help 始终优先：即使其余参数 malformed 也直接输出帮助后退出 0 */
    if (cli_has_help(argc, argv)) {
        const char *sub = cli_subcommand(argc, argv);
        if (sub) {
            char cfgbuf[512];
            const char *cp = resolve_cfgpath(pc.config, cfgbuf, sizeof cfgbuf);
            config_t cfg;
            if (config_load(cp, &cfg) == 0) registry_init(&cfg);
            help_print_subcommand(sub, pc.uid[0] ? pc.uid : NULL);
        } else {
            help_print_global();
        }
        return 0;
    }
    if (argc < 2) { help_print_global(); return 2; }
    if (rc != 0 || !pc.op) {
        printf("{\"status\":\"error\",\"op\":\"parse\",\"error\":{\"code\":2,\"message\":\"%s\"}}\n",
               cli_get_error());
        return 2;
    }

    char cfgbuf[512];
    const char *cfgpath = resolve_cfgpath(pc.config, cfgbuf, sizeof cfgbuf);
    config_t cfg;
    if (config_load(cfgpath, &cfg) != 0) {
        fprintf(stderr, "config load failed: %s\n", cfgpath);
        return 1;
    }
    registry_init(&cfg);
    const char *sf = cfg.state_file[0] ? cfg.state_file : "~/.demoncat/state.json";
    char sfbuf[512];
    if (sf[0] == '~') {
        const char *home = getenv("HOME");
        if (home) snprintf(sfbuf, sizeof sfbuf, "%s%s", home, sf + 1);
        else { strncpy(sfbuf, sf, sizeof(sfbuf) - 1); sfbuf[sizeof(sfbuf) - 1] = '\0'; }
        state_set_file(sfbuf);
    } else {
        state_set_file(sf);
    }
    state_load();

    char defplugins[512];
    const char *plugindir = pc.plugins;
    if (!plugindir) {
        char root[256];
        derive_project_root(cfgpath, root, sizeof root);
        snprintf(defplugins, sizeof defplugins, "%s/plugins", root);
        plugindir = defplugins;
    }
    plugin_load_dir(plugindir);

    result_t *r;
    if (pc.all) {
        if (!pc.op || strcmp(pc.op, "clean") != 0) {
            printf("{\"status\":\"error\",\"op\":\"parse\",\"error\":{\"code\":2,\"message\":\"--all only valid with clean\"}}\n");
            return 2;
        }
        r = dispatch_clean_all();
    } else {
        r = dispatch_route_force(pc.uid, pc.op, &pc.params, pc.force);
    }
    output_print(r);
    int code = r ? r->code : 1;
    result_free(r);
    state_save();
    plugin_fini();
    return code;
}
