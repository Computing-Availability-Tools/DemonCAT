/*
 * _npu_stress.cpp — NPU stress tool using aclnn operators (no torch_npu required).
 * Usage: _npu_stress <aicore|aicpu|aivector|hbm> <dev_id> [size] [load_pct] [duration_sec]
 *   aicore:   aclnnMatmul (Cube units → AICore Usage)
 *   aicpu:    aclnnTopk FP64 (AICpu units → AICpu Usage)
 *   aivector: aclnnExp / aclnnAdd (Vector units → AIVector Usage)
 *   hbm:      aclrtMalloc + memset (HBM memory stress)
 * duration 0 = run forever (until killed)
 * load_pct 1-100 (default 100). 满血/PWM 两套配置(见 npu_stress_cfg.h):
 *   load_pct>=100 (默认): 满血直跑, 对齐华为 Python 参考脚本大算子
 *       aicore aclnnMatmul FP32 5120 (FP16 带宽受限仅 96%)
 *       aivector aclnnAdd FP32 8500 (PWM 用 exp 8192 仅 84%)
 *       aicpu aclnnTopk FP64 2000
 *   load_pct<100: 固定 50ms PWM 占空比, duty = load_pct / max_achievable
 *       (aicore 0.96, aicpu 0.94, aivector 0.84)
 * Monitor: npu-smi info -t usages -i <card> -c 0
 */
#include "acl/acl.h"
#include "aclnn/aclnn_base.h"
#include "aclnn/acl_meta.h"
#include "aclnnop/aclnn_matmul.h"
#include "aclnnop/aclnn_topk.h"
#include "aclnnop/aclnn_exp.h"
#include "aclnnop/aclnn_add.h"
#include "npu_stress_cfg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

static const char *usage = "Usage: _npu_stress <aicore|aicpu|aivector|hbm> <dev_id> [size] [load_pct] [duration_sec]\n"
                           "  duration 0 = run forever (until killed)\n"
                           "  load_pct 1-100 (default 100): fixed-period PWM duty-cycle\n";

/* Ensure ASCEND_OPP_PATH matches the toolkit we're linked against.
 * _npu_stress RPATH = ascend-toolkit/lib64, so OPP must be from the same toolkit.
 * If shell set ASCEND_OPP_PATH to nnae/other CANN package, it won't match → 561103. */
static void ensure_opp_path(void) {
    /* 1. ASCEND_TOOLKIT_HOME set → always prefer it (matches linked libs) */
    const char *tk = getenv("ASCEND_TOOLKIT_HOME");
    if (tk && tk[0]) {
        char path[512];
        snprintf(path, sizeof(path), "%s/opp", tk);
        if (access(path, F_OK) == 0) {
            setenv("ASCEND_OPP_PATH", path, 1);
            return;
        }
    }
    /* 2. ASCEND_OPP_PATH already set → keep only if it has tiling config */
    const char *opp = getenv("ASCEND_OPP_PATH");
    if (opp && opp[0]) {
        char check[512];
        snprintf(check, sizeof(check), "%s/op_api", opp);
        if (access(check, F_OK) == 0) return; /* looks valid */
    }
    /* 3. Fallback: try known toolkit paths */
    const char *candidates[] = {
        "/usr/local/Ascend/ascend-toolkit/latest/opp",
        "/usr/local/Ascend/latest/opp",
        NULL};
    for (int i = 0; candidates[i]; i++) {
        if (access(candidates[i], F_OK) == 0) {
            setenv("ASCEND_OPP_PATH", candidates[i], 1);
            return;
        }
    }
}

static aclTensor *make_tensor(void *dev_ptr, int rows, int cols, aclDataType dtype) {
    int64_t dims[2] = {rows, cols};
    int64_t stride[2] = {cols, 1};
    return aclCreateTensor(dims, 2, dtype, stride, 0, ACL_FORMAT_ND, dims, 2, dev_ptr);
}

static long elapsed_us(struct timespec *start) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - start->tv_sec) * 1000000 + (now.tv_nsec - start->tv_nsec) / 1000;
}

/* Operator context: holds all params needed for two-stage ACL call */
struct OpCtx {
    int mode;                     /* 0=matmul, 1=topk, 2=add */
    aclTensor *tA, *tB, *tC, *tD; /* tD only for topk indices */
    aclScalar *alpha;             /* only for add */
    int64_t k;                    /* only for topk */
    uint64_t ws_size;
    aclOpExecutor *exec;
    void *ws;
};

/* Run one batch: GetWorkspaceSize + Execute (two-stage API, executor must be re-fetched each call) */
static void run_batch(aclrtStream stream, struct OpCtx *ctx) {
    aclnnStatus s;
    switch (ctx->mode) {
    case NPU_STRESS_MATMUL: /* matmul */
        s = aclnnMatmulGetWorkspaceSize(ctx->tA, ctx->tB, ctx->tC, 0, &ctx->ws_size, &ctx->exec);
        if (s == 0 && ctx->exec) aclnnMatmul(ctx->ws, ctx->ws_size, ctx->exec, stream);
        break;
    case NPU_STRESS_TOPK: /* topk */
        s = aclnnTopkGetWorkspaceSize(ctx->tA, ctx->k, 1, true, true, ctx->tC, ctx->tD, &ctx->ws_size, &ctx->exec);
        if (s == 0 && ctx->exec) aclnnTopk(ctx->ws, ctx->ws_size, ctx->exec, stream);
        break;
    case NPU_STRESS_ADD: /* add: C = A + B */
        s = aclnnAddGetWorkspaceSize(ctx->tA, ctx->tB, ctx->alpha, ctx->tC, &ctx->ws_size, &ctx->exec);
        if (s == 0 && ctx->exec) aclnnAdd(ctx->ws, ctx->ws_size, ctx->exec, stream);
        break;
    case NPU_STRESS_EXP: /* exp */
        s = aclnnExpGetWorkspaceSize(ctx->tA, ctx->tC, &ctx->ws_size, &ctx->exec);
        if (s == 0 && ctx->exec) aclnnExp(ctx->ws, ctx->ws_size, ctx->exec, stream);
        break;
    }
}

#define PWM_WINDOW_US 50000 /* 50ms fixed window */

/* Run stress loop with optional PWM duty-cycle */
static void run_loop(int load_pct, int duration, float max_achievable,
                     aclrtStream stream, struct OpCtx *ctx) {
    if (load_pct >= 100) {
        /* Full speed: batch=100 to amortize sync overhead, check time inside loop */
        if (duration > 0) {
            time_t end = time(NULL) + duration;
            while (time(NULL) < end) {
                for (int i = 0; i < 100; i++) {
                    if (time(NULL) >= end) break;
                    run_batch(stream, ctx);
                }
                aclrtSynchronizeStream(stream);
            }
        } else {
            while (1) {
                for (int i = 0; i < 100; i++) run_batch(stream, ctx);
                aclrtSynchronizeStream(stream);
            }
        }
        return;
    }

    /* PWM: fixed 50ms window */
    int duty = (int)(load_pct / max_achievable);
    if (duty < 1) duty = 1;
    if (duty > 100) duty = 100;
    long compute_us = (long)PWM_WINDOW_US * duty / 100;

    if (duration > 0) {
        time_t end = time(NULL) + duration;
        while (time(NULL) < end) {
            struct timespec win_start;
            clock_gettime(CLOCK_MONOTONIC, &win_start);
            while (elapsed_us(&win_start) < compute_us) {
                run_batch(stream, ctx);
                aclrtSynchronizeStream(stream);
            }
            long sleep_us = (long)PWM_WINDOW_US - elapsed_us(&win_start);
            if (sleep_us > 0) usleep(sleep_us);
        }
    } else {
        while (1) {
            struct timespec win_start;
            clock_gettime(CLOCK_MONOTONIC, &win_start);
            while (elapsed_us(&win_start) < compute_us) {
                run_batch(stream, ctx);
                aclrtSynchronizeStream(stream);
            }
            long sleep_us = (long)PWM_WINDOW_US - elapsed_us(&win_start);
            if (sleep_us > 0) usleep(sleep_us);
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "%s", usage);
        return 1;
    }

    const char *mode = argv[1];
    int dev_id = atoi(argv[2]);
    int size = 0;
    if (argc > 3) size = atoi(argv[3]);
    int load_pct = 100;
    if (argc > 4) load_pct = atoi(argv[4]);
    int duration = 0;
    if (argc > 5) duration = atoi(argv[5]);
    if (load_pct < 1) load_pct = 1;
    if (load_pct > 100) load_pct = 100;

    ensure_opp_path();

    aclError ret = aclInit(NULL);
    if (ret != ACL_SUCCESS) {
        fprintf(stderr, "aclInit fail: %d\n", ret);
        return 1;
    }
    ret = aclrtSetDevice(dev_id);
    if (ret != ACL_SUCCESS) {
        fprintf(stderr, "aclrtSetDevice(%d) fail: %d\n", dev_id, ret);
        aclFinalize();
        return 1;
    }
    aclnnInit(NULL);

    aclrtStream stream;
    aclrtCreateStream(&stream);

    if (strcmp(mode, "hbm") == 0) {
        int size_mb = size > 0 ? size : 512;
        size_t bytes = (size_t)size_mb * 1024 * 1024;
        void *d_ptr = NULL;
        ret = aclrtMalloc(&d_ptr, bytes, ACL_MEM_MALLOC_HUGE_FIRST);
        if (ret != ACL_SUCCESS) {
            fprintf(stderr, "aclrtMalloc %dMB fail: %d\n", size_mb, ret);
            goto fail;
        }
        aclrtMemset(d_ptr, bytes, 0xAA, bytes);
        printf("HBM stress: %dMB on dev %d %s\n", size_mb, dev_id, duration > 0 ? "" : "forever");
        fflush(stdout);
        if (duration > 0) {
            sleep(duration);
        } else {
            while (1) pause();
        }
        aclrtFree(d_ptr);

    } else {
        struct npu_stress_cfg cfg = npu_stress_cfg(mode, load_pct, size);
        int op_mode = cfg.op;
        int shape = cfg.shape;
        float max_achievable = cfg.max_achievable; /* PWM 校准: aicore 0.96, aicpu 0.94, aivector 0.84 */

        if (op_mode < 0) {
            fprintf(stderr, "unknown mode: %s\n%s", mode, usage);
            goto fail;
        }

        /* Element size: FP16/FP32=2/4 bytes, DOUBLE=8 bytes */
        int elem_sz = 2;
        if (cfg.dtype == NPU_STRESS_FP32)
            elem_sz = 4;
        else if (cfg.dtype == NPU_STRESS_FP64)
            elem_sz = 8;
        size_t sz = (size_t)shape * shape * elem_sz;

        struct OpCtx ctx;
        memset(&ctx, 0, sizeof(ctx));
        ctx.mode = op_mode;

        void *dA = NULL, *dB = NULL, *dC = NULL, *dD = NULL;

        if (aclrtMalloc(&dA, sz, ACL_MEM_MALLOC_HUGE_FIRST) ||
            aclrtMalloc(&dB, sz, ACL_MEM_MALLOC_HUGE_FIRST) ||
            aclrtMalloc(&dC, sz, ACL_MEM_MALLOC_HUGE_FIRST)) {
            fprintf(stderr, "malloc fail (shape=%d)\n", shape);
            goto fail;
        }
        aclrtMemset(dA, sz, 0x00, sz);
        aclrtMemset(dB, sz, 0x00, sz);

        aclDataType in_dtype = ACL_FLOAT16;
        if (cfg.dtype == NPU_STRESS_FP32)
            in_dtype = ACL_FLOAT;
        else if (cfg.dtype == NPU_STRESS_FP64)
            in_dtype = ACL_DOUBLE;
        ctx.tA = make_tensor(dA, shape, shape, in_dtype);
        ctx.tB = make_tensor(dB, shape, shape, in_dtype);
        ctx.tC = make_tensor(dC, shape, shape, in_dtype);

        aclnnStatus s = -1;

        if (op_mode == 0) {
            /* matmul: C = A * B */
            s = aclnnMatmulGetWorkspaceSize(ctx.tA, ctx.tB, ctx.tC, 0, &ctx.ws_size, &ctx.exec);
        } else if (op_mode == 1) {
            /* topk with FP64 forces AICPU execution (Vector/Cube don't support float64) */
            int64_t k = shape - 1;
            if (k > 1000) k = 1000;
            ctx.k = k;
            /* output shape: {shape, k} */
            size_t out_sz = (size_t)shape * k * 8; /* DOUBLE or INT64 (both 8 bytes) */
            if (aclrtMalloc(&dD, out_sz, ACL_MEM_MALLOC_HUGE_FIRST)) {
                fprintf(stderr, "topk out malloc fail\n");
                goto fail;
            }
            int64_t out_dims[2] = {shape, k};
            int64_t out_stride[2] = {k, 1};
            ctx.tC = aclCreateTensor(out_dims, 2, ACL_DOUBLE, out_stride, 0, ACL_FORMAT_ND, out_dims, 2, dC);
            ctx.tD = aclCreateTensor(out_dims, 2, ACL_INT64, out_stride, 0, ACL_FORMAT_ND, out_dims, 2, dD);
            s = aclnnTopkGetWorkspaceSize(ctx.tA, k, 1, true, true, ctx.tC, ctx.tD, &ctx.ws_size, &ctx.exec);
        } else if (op_mode == 2) {
            /* add: C = A + alpha*B, alpha=1.0 (FP32, 满血) */
            float one = 1.0f;
            ctx.alpha = aclCreateScalar(&one, ACL_FLOAT);
            s = aclnnAddGetWorkspaceSize(ctx.tA, ctx.tB, ctx.alpha, ctx.tC, &ctx.ws_size, &ctx.exec);
        } else {
            /* exp: C = exp(A), input=0.0 → exp(0)=1.0, no overflow */
            s = aclnnExpGetWorkspaceSize(ctx.tA, ctx.tC, &ctx.ws_size, &ctx.exec);
        }

        if (s != 0 || !ctx.exec) {
            fprintf(stderr, "GetWorkspaceSize fail: %d (mode=%s, shape=%d)\n", s, mode, shape);
            goto fail;
        }
        if (ctx.ws_size > 0) aclrtMalloc(&ctx.ws, ctx.ws_size, ACL_MEM_MALLOC_HUGE_FIRST);

        const char *dtype_str = cfg.dtype == NPU_STRESS_FP32   ? "FP32"
                                : cfg.dtype == NPU_STRESS_FP64 ? "FP64"
                                                               : "FP16";
        const char *label = op_mode == 0 ? "AICore(matmul)" : op_mode == 1 ? "AICpu(topk)"
                                                          : op_mode == 2   ? "AIVector(add)"
                                                                           : "AIVector(exp)";
        printf("%s: %dx%d %s on dev %d load=%d%% %s%s\n", label, shape, shape, dtype_str, dev_id, load_pct,
               cfg.fullpower ? "(fullpower) " : "", duration > 0 ? "" : "forever");
        fflush(stdout);

        run_loop(load_pct, duration, max_achievable, stream, &ctx);

        if (ctx.ws) aclrtFree(ctx.ws);
        if (ctx.alpha) aclDestroyScalar(ctx.alpha);
        aclDestroyTensor(ctx.tA);
        aclDestroyTensor(ctx.tB);
        if (ctx.tC) aclDestroyTensor(ctx.tC);
        if (ctx.tD) aclDestroyTensor(ctx.tD);
        aclrtFree(dA);
        aclrtFree(dB);
        aclrtFree(dC);
        if (dD) aclrtFree(dD);
    }

    aclrtDestroyStream(stream);
    aclrtResetDevice(dev_id);
    aclnnFinalize();
    aclFinalize();
    return 0;

fail:
    aclrtDestroyStream(stream);
    aclrtResetDevice(dev_id);
    aclnnFinalize();
    aclFinalize();
    return 1;
}
