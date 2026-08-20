/*
 * _npu_stress.cpp — NPU stress tool using aclnn operators (no torch_npu required).
 * Usage: _npu_stress <aicore|aicpu|aivector|hbm> <dev_id> [size] [load_pct] [duration_sec]
 *   aicore:   aclnnMatmul 5120 (Cube units → AICore Usage)
 *   aicpu:    aclnnTopk 2000 (AICpu units → AICpu Usage)
 *   aivector: aclnnAdd 8192 (Vector units → AIVector Usage)
 *   hbm:      aclrtMalloc + memset (HBM memory stress)
 * duration 0 = run forever (until killed)
 * load_pct 1-100 (default 100): fixed 50ms PWM duty-cycle.
 *   duty = load_pct / max_achievable (aicore 0.96, aicpu 0.90, aivector 0.98)
 * Monitor: npu-smi info -t usages -i <card> -c 0
 */
#include "acl/acl.h"
#include "aclnn/aclnn_base.h"
#include "aclnn/acl_meta.h"
#include "aclnnop/aclnn_matmul.h"
#include "aclnnop/aclnn_topk.h"
#include "aclnnop/aclnn_add.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

static const char *usage = "Usage: _npu_stress <aicore|aicpu|aivector|hbm> <dev_id> [size] [load_pct] [duration_sec]\n"
                           "  duration 0 = run forever (until killed)\n"
                           "  load_pct 1-100 (default 100): fixed-period PWM duty-cycle\n";

static aclTensor* make_tensor(void *dev_ptr, int rows, int cols, aclDataType dtype) {
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
    int mode;  /* 0=matmul, 1=topk, 2=add */
    aclTensor *tA, *tB, *tC, *tD;  /* tD only for topk indices */
    aclScalar *alpha;  /* only for add */
    int64_t k;  /* only for topk */
    uint64_t ws_size;
    aclOpExecutor *exec;
    void *ws;
};

/* Run one batch: GetWorkspaceSize + Execute (two-stage API, executor must be re-fetched each call) */
static void run_batch(aclrtStream stream, struct OpCtx *ctx) {
    aclnnStatus s;
    switch (ctx->mode) {
    case 0: /* matmul */
        s = aclnnMatmulGetWorkspaceSize(ctx->tA, ctx->tB, ctx->tC, 0, &ctx->ws_size, &ctx->exec);
        if (s == 0 && ctx->exec) aclnnMatmul(ctx->ws, ctx->ws_size, ctx->exec, stream);
        break;
    case 1: /* topk */
        s = aclnnTopkGetWorkspaceSize(ctx->tA, ctx->k, 1, true, true, ctx->tC, ctx->tD, &ctx->ws_size, &ctx->exec);
        if (s == 0 && ctx->exec) aclnnTopk(ctx->ws, ctx->ws_size, ctx->exec, stream);
        break;
    case 2: /* add */
        s = aclnnAddGetWorkspaceSize(ctx->tA, ctx->tB, ctx->alpha, ctx->tC, &ctx->ws_size, &ctx->exec);
        if (s == 0 && ctx->exec) aclnnAdd(ctx->ws, ctx->ws_size, ctx->exec, stream);
        break;
    }
}

#define PWM_WINDOW_US 50000  /* 50ms fixed window */

/* Run stress loop with optional PWM duty-cycle */
static void run_loop(int load_pct, int duration, float max_achievable,
                     aclrtStream stream, struct OpCtx *ctx) {
    if (load_pct >= 100) {
        /* Full speed, no PWM */
        if (duration > 0) {
            time_t end = time(NULL) + duration;
            while (time(NULL) < end) {
                for (int i = 0; i < 100; i++) run_batch(stream, ctx);
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
                for (int i = 0; i < 10; i++) run_batch(stream, ctx);
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
                for (int i = 0; i < 10; i++) run_batch(stream, ctx);
                aclrtSynchronizeStream(stream);
            }
            long sleep_us = (long)PWM_WINDOW_US - elapsed_us(&win_start);
            if (sleep_us > 0) usleep(sleep_us);
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "%s", usage); return 1; }

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

    aclError ret = aclInit(NULL);
    if (ret != ACL_SUCCESS) { fprintf(stderr, "aclInit fail: %d\n", ret); return 1; }
    ret = aclrtSetDevice(dev_id);
    if (ret != ACL_SUCCESS) { fprintf(stderr, "aclrtSetDevice(%d) fail: %d\n", dev_id, ret); aclFinalize(); return 1; }
    aclnnInit(NULL);

    aclrtStream stream;
    aclrtCreateStream(&stream);

    if (strcmp(mode, "hbm") == 0) {
        int size_mb = size > 0 ? size : 512;
        size_t bytes = (size_t)size_mb * 1024 * 1024;
        void *d_ptr = NULL;
        ret = aclrtMalloc(&d_ptr, bytes, ACL_MEM_MALLOC_HUGE_FIRST);
        if (ret != ACL_SUCCESS) { fprintf(stderr, "aclrtMalloc %dMB fail: %d\n", size_mb, ret); goto fail; }
        aclrtMemset(d_ptr, bytes, 0xAA, bytes);
        printf("HBM stress: %dMB on dev %d %s\n", size_mb, dev_id, duration > 0 ? "" : "forever");
        fflush(stdout);
        if (duration > 0) { sleep(duration); } else { while (1) pause(); }
        aclrtFree(d_ptr);

    } else {
        int base_shape;
        float max_achievable;
        int op_mode;

        if (strcmp(mode, "aicore") == 0) {
            base_shape = 5120; max_achievable = 0.96f; op_mode = 0;
        } else if (strcmp(mode, "aicpu") == 0) {
            base_shape = 2000; max_achievable = 0.90f; op_mode = 1;
        } else if (strcmp(mode, "aivector") == 0) {
            base_shape = 8192; max_achievable = 0.98f; op_mode = 2;
        } else {
            fprintf(stderr, "unknown mode: %s\n%s", mode, usage);
            goto fail;
        }

        int shape = size > 0 ? size : base_shape;
        size_t sz = (size_t)shape * shape * 2;  /* FP16 input */

        struct OpCtx ctx;
        memset(&ctx, 0, sizeof(ctx));
        ctx.mode = op_mode;

        void *dA=NULL, *dB=NULL, *dC=NULL, *dD=NULL;

        if (aclrtMalloc(&dA, sz, ACL_MEM_MALLOC_HUGE_FIRST) ||
            aclrtMalloc(&dB, sz, ACL_MEM_MALLOC_HUGE_FIRST) ||
            aclrtMalloc(&dC, sz, ACL_MEM_MALLOC_HUGE_FIRST)) {
            fprintf(stderr, "malloc fail (shape=%d)\n", shape); goto fail;
        }
        aclrtMemset(dA, sz, 0x00, sz);
        aclrtMemset(dB, sz, 0x00, sz);

        ctx.tA = make_tensor(dA, shape, shape, ACL_FLOAT16);
        ctx.tB = make_tensor(dB, shape, shape, ACL_FLOAT16);
        ctx.tC = make_tensor(dC, shape, shape, ACL_FLOAT16);

        aclnnStatus s = -1;

        if (op_mode == 0) {
            /* matmul: C = A * B */
            s = aclnnMatmulGetWorkspaceSize(ctx.tA, ctx.tB, ctx.tC, 0, &ctx.ws_size, &ctx.exec);
        } else if (op_mode == 1) {
            /* topk: need values(FP16) + indices(INT64) output */
            int64_t k = shape / 2;
            ctx.k = k;
            /* output shape: {shape, k} */
            size_t out_sz = (size_t)shape * k * 8;  /* INT64 (largest) */
            if (aclrtMalloc(&dD, out_sz, ACL_MEM_MALLOC_HUGE_FIRST)) {
                fprintf(stderr, "topk out malloc fail\n"); goto fail;
            }
            int64_t out_dims[2] = {shape, k};
            int64_t out_stride[2] = {k, 1};
            ctx.tC = aclCreateTensor(out_dims, 2, ACL_FLOAT16, out_stride, 0, ACL_FORMAT_ND, out_dims, 2, dC);
            ctx.tD = aclCreateTensor(out_dims, 2, ACL_INT64, out_stride, 0, ACL_FORMAT_ND, out_dims, 2, dD);
            s = aclnnTopkGetWorkspaceSize(ctx.tA, k, 1, true, true, ctx.tC, ctx.tD, &ctx.ws_size, &ctx.exec);
        } else {
            /* add: C = A + B */
            float alpha_val = 1.0f;
            ctx.alpha = aclCreateScalar(&alpha_val, ACL_FLOAT);
            s = aclnnAddGetWorkspaceSize(ctx.tA, ctx.tB, ctx.alpha, ctx.tC, &ctx.ws_size, &ctx.exec);
        }

        if (s != 0 || !ctx.exec) {
            fprintf(stderr, "GetWorkspaceSize fail: %d (mode=%s, shape=%d)\n", s, mode, shape);
            goto fail;
        }
        if (ctx.ws_size > 0) aclrtMalloc(&ctx.ws, ctx.ws_size, ACL_MEM_MALLOC_HUGE_FIRST);

        const char *label = op_mode == 0 ? "AICore(matmul)" : op_mode == 1 ? "AICpu(topk)" : "AIVector(add)";
        printf("%s: %dx%d FP16 on dev %d load=%d%% %s\n", label, shape, shape, dev_id, load_pct,
               duration > 0 ? "" : "forever");
        fflush(stdout);

        run_loop(load_pct, duration, max_achievable, stream, &ctx);

        if (ctx.ws) aclrtFree(ctx.ws);
        if (ctx.alpha) aclDestroyScalar(ctx.alpha);
        aclDestroyTensor(ctx.tA); aclDestroyTensor(ctx.tB);
        if (ctx.tC) aclDestroyTensor(ctx.tC);
        if (ctx.tD) aclDestroyTensor(ctx.tD);
        aclrtFree(dA); aclrtFree(dB); aclrtFree(dC);
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
