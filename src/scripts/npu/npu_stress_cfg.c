/* npu_stress_cfg.c — 满血/PWM 算子配置决策实现
 * 与 _npu_stress.cpp(AcL 主程序) 共用；纯 C，可被 UT 单测。 */
#include "npu_stress_cfg.h"
#include <string.h>

struct npu_stress_cfg npu_stress_cfg(const char *mode, int load_pct, int size) {
    struct npu_stress_cfg c;
    const int fullpower = (load_pct >= 100) ? 1 : 0;

    memset(&c, 0, sizeof(c));
    c.fullpower = fullpower;

    if (strcmp(mode, "aicore") == 0) {
        if (fullpower) {
            c.op = NPU_STRESS_MATMUL;
            c.dtype = NPU_STRESS_FP32; /* 满血换 FP32(H16 带宽受限仅 96%) */
            c.shape = size > 0 ? size : 5120;
        } else {
            c.op = NPU_STRESS_MATMUL;
            c.dtype = NPU_STRESS_FP16;
            c.shape = size > 0 ? size : 5120;
        }
        c.max_achievable = 0.96f;
    } else if (strcmp(mode, "aicpu") == 0) {
        c.op = NPU_STRESS_TOPK;
        c.dtype = NPU_STRESS_FP64;
        c.shape = size > 0 ? size : (fullpower ? 2000 : 500); /* 满血 shape 2000(Python 参考) */
        c.max_achievable = 0.94f;
    } else if (strcmp(mode, "aivector") == 0) {
        if (fullpower) {
            c.op = NPU_STRESS_ADD; /* 满血改 add(Python 参考) */
            c.dtype = NPU_STRESS_FP32;
            c.shape = size > 0 ? size : 8500;
        } else {
            c.op = NPU_STRESS_EXP;
            c.dtype = NPU_STRESS_FP16;
            c.shape = size > 0 ? size : 8192;
        }
        c.max_achievable = 0.84f;
    } else {
        c.op = -1; /* 未知 mode 标记无效 */
        c.dtype = NPU_STRESS_FP16;
        c.shape = 0;
    }
    return c;
}
