/*
 * _npu_stress.cpp — NPU stress tool using aclnn operators (no torch_npu required).
 * Usage: _npu_stress <aicore|aicpu|aivector|hbm> <dev_id> [size] [load_pct] [duration_sec]
 *   aicore:   aclnnMatmul 5120 (Cube units → AICore Usage)
 *   aicpu:    aclnnTopk 2000 (AICpu units → AICpu Usage)
 *   aivector: aclnnAdd 8192 (Vector units → AIVector Usage)
 *   hbm:      aclrtMalloc + memset (HBM memory stress)
 * duration 0 = run forever (until killed)
 * load_pct 1-100 (default 100): fixed-period PWM duty-cycle.
 *   100 = full speed. 50 = 50% duty within 50ms window (compute 25ms, sleep 25ms).
 *   Calibration: aicore 0.96, aicpu 0.90, aivector 0.98 (满负荷也到不了100%).
 *   Fixed 50ms window → npu-smi 采样窗口内看到多个周期, 平均值平滑稳定.
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

#define PWM_WINDOW_US 50000  /* 50ms fixed window */

/* Run PWM loop: compute for window*duty, sleep for window*(1-duty).
 * batch_fn is called repeatedly during the compute phase. */
static void run_pwm_loop(int load_pct, int duration, float max_achievable,
                         void (*batch_fn)(aclrtStream, void*, uint64_t, aclOpExecutor*),
                         aclrtStream stream, void *ws, uint64_t ws_size, aclOpExecutor *exec) {
    if (load_pct >= 100) {
        /* Full speed, no PWM */
        if (duration > 0) {
            time_t end = time(NULL) + duration;
            while (time(NULL) < end) {
                for (int i = 0; i < 100; i++) batch_fn(stream, ws, ws_size, exec);
                aclrtSynchronizeStream(stream);
            }
        } else {
            while (1) {
                for (int i = 0; i < 100; i++) batch_fn(stream, ws, ws_size, exec);
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
            struct timespec ws_start;
            clock_gettime(CLOCK_MONOTONIC, &ws_start);
            /* compute phase */
            while (elapsed_us(&ws_start) < compute_us) {
                for (int i = 0; i < 10; i++) batch_fn(stream, ws, ws_size, exec);
                aclrtSynchronizeStream(stream);
            }
            /* sleep phase */
            long sleep_us = (long)PWM_WINDOW_US - elapsed_us(&ws_start);
            if (sleep_us > 0) usleep(sleep_us);
        }
    } else {
        while (1) {
            struct timespec ws_start;
            clock_gettime(CLOCK_MONOTONIC, &ws_start);
            while (elapsed_us(&ws_start) < compute_us) {
                for (int i = 0; i < 10; i++) batch_fn(stream, ws, ws_size, exec);
                aclrtSynchronizeStream(stream);
            }
            long sleep_us = (long)PWM_WINDOW_US - elapsed_us(&ws_start);
            if (sleep_us > 0) usleep(sleep_us);
        }
    }
}

static void batch_matmul(aclrtStream s, void *ws, uint64_t sz, aclOpExecutor *ex) {
    aclnnMatmul(ws, sz, ex, s);
}
static void batch_topk(aclrtStream s, void *ws, uint64_t sz, aclOpExecutor *ex) {
    aclnnTopk(ws, sz, ex, s);
}
static void batch_add(aclrtStream s, void *ws, uint64_t sz, aclOpExecutor *ex) {
    aclnnAdd(ws, sz, ex, s);
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
        if (strcmp(mode, "aicore") == 0)         { base_shape = 5120; max_achievable = 0.96f; }
        else if (strcmp(mode, "aicpu") == 0)    { base_shape = 2000; max_achievable = 0.90f; }
        else if (strcmp(mode, "aivector") == 0) { base_shape = 8192; max_achievable = 0.98f; }
        else { fprintf(stderr, "unknown mode: %s\n%s", mode, usage); goto fail; }

        int shape = size > 0 ? size : base_shape;
        size_t sz = (size_t)shape * shape * 2;
        void *dA=NULL, *dB=NULL, *dC=NULL, *dD=NULL;
        if (aclrtMalloc(&dA, sz, ACL_MEM_MALLOC_HUGE_FIRST) ||
            aclrtMalloc(&dB, sz, ACL_MEM_MALLOC_HUGE_FIRST) ||
            aclrtMalloc(&dC, sz, ACL_MEM_MALLOC_HUGE_FIRST)) {
            fprintf(stderr, "malloc fail (shape=%d)\n", shape); goto fail;
        }
        aclrtMemset(dA, sz, 0x00, sz); aclrtMemset(dB, sz, 0x00, sz);
        aclTensor *tA = make_tensor(dA, shape, shape, ACL_FLOAT16);
        aclTensor *tB = make_tensor(dB, shape, shape, ACL_FLOAT16);
        aclTensor *tC = make_tensor(dC, shape, shape, ACL_FLOAT16);

        uint64_t ws_size = 0;
        aclOpExecutor *exec = NULL;
        aclnnStatus s = -1;
        void *ws = NULL;
        void (*batch_fn)(aclrtStream, void*, uint64_t, aclOpExecutor*) = NULL;

        if (strcmp(mode, "aicore") == 0) {
            s = aclnnMatmulGetWorkspaceSize(tA, tB, tC, 0, &ws_size, &exec);
            batch_fn = batch_matmul;
        } else if (strcmp(mode, "aicpu") == 0) {
            if (aclrtMalloc(&dD, sz, ACL_MEM_MALLOC_HUGE_FIRST)) {
                fprintf(stderr, "topk out malloc fail\n"); goto fail;
            }
            aclTensor *tD = make_tensor(dD, shape, shape, ACL_INT64);
            int64_t k = shape / 2;
            s = aclnnTopkGetWorkspaceSize(tA, k, 1, true, true, tC, tD, &ws_size, &exec);
            batch_fn = batch_topk;
            aclDestroyTensor(tD);
        } else {
            float alpha_val = 1.0f;
            aclScalar *alpha = aclCreateScalar(&alpha_val, ACL_FLOAT);
            s = aclnnAddGetWorkspaceSize(tA, tB, alpha, tC, &ws_size, &exec);
            batch_fn = batch_add;
            aclDestroyScalar(alpha);
        }

        if (s != 0 || !exec) {
            fprintf(stderr, "GetWorkspaceSize fail: %d (mode=%s, shape=%d)\n", s, mode, shape);
            goto fail;
        }
        if (ws_size > 0) aclrtMalloc(&ws, ws_size, ACL_MEM_MALLOC_HUGE_FIRST);

        const char *label = strcmp(mode, "aicore") == 0 ? "AICore(matmul)" :
                            strcmp(mode, "aicpu") == 0 ? "AICpu(topk)" : "AIVector(add)";
        printf("%s: %dx%d FP16 on dev %d load=%d%% %s\n", label, shape, shape, dev_id, load_pct,
               duration > 0 ? "" : "forever");
        fflush(stdout);

        run_pwm_loop(load_pct, duration, max_achievable, batch_fn, stream, ws, ws_size, exec);

        if (ws) aclrtFree(ws);
        aclDestroyTensor(tA); aclDestroyTensor(tB); aclDestroyTensor(tC);
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
