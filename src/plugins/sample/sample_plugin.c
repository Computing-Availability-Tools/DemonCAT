/* 示例动态插件：可恢复故障 rSAMPLE_test（inject/clean/query）
 * 编译为 plugins/libsample.so，验证 dlopen 加载 + dispatch 三级回退 + state */
#include "plugins/plugin.h"
#include "core/types.h"
#include "core/output.h"

static result_t *sample_inject(const params_t *params) {
    (void)params;
    return result_ok("inject", "rSAMPLE_test", 0, "sample injected");
}
static result_t *sample_clean(const params_t *params) {
    (void)params;
    return result_ok("clean", "rSAMPLE_test", 0, "sample cleaned");
}
static result_t *sample_query(const params_t *params) {
    (void)params;
    return result_ok("query", "rSAMPLE_test", 0, "sample confirmed");
}

const dcat_plugin_t *dcat_plugin_get(void) {
    static const dcat_plugin_t p = {
        .abi_version = DCAT_PLUGIN_ABI_VERSION,
        .name = "sample",
        .description = "sample plugin for integration test",
        .uid = "rSAMPLE_test",
        .supported_ops = "inject,clean,query",
        .required_params = "",
        .optional_params = "",
        .init = NULL,
        .fini = NULL,
        .precheck = NULL,
        .inject = sample_inject,
        .clean = sample_clean,
        .query = sample_query,
    };
    return &p;
}
