/*
 * _npu_stress.c — NPU stress tool using ACL BLAS + aclnn (no torch_npu required).
 * Usage: _npu_stress <hbm|aicore|aivector> <device_id> <duration_sec> [size_mb] [load_pct]
 *   hbm:      allocate HBM memory + memset (HBM bandwidth stress)
 *   aicore:   FP32 GEMM loop (Cube compute unit stress)
 *   aivector: FP32 element-wise Add loop (Vector compute unit stress)
 * duration 0 = run forever (until killed)
 * load_pct 1-100 (default 100), duty-cycle: compute for X ms, sleep for Y ms
 * Monitor: npu-smi info -t usages -i <card> -c 0
 */
#include "acl/acl.h"
#include "acl/ops/acl_cblas.h"
#include "aclnn/aclnn_base.h"
#include "aclnn/acl_meta.h"
#include "aclnnop/aclnn_exp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

static const char *usage = "Usage: _npu_stress <hbm|aicore|aivector> <device_id> <duration_sec> [size_mb] [load_pct]\n"
                           "  duration 0 = run forever (until killed)\n"
                           "  load_pct 1-100 (default 100)\n";

/* duty-cycle helper: returns 1 if still in compute phase, 0 if should sleep */
static int in_compute_phase(int load_pct, struct timespec *cycle_start) {
    if (load_pct >= 100) return 1;
    int cycle_ms = 100;
    int on_ms = cycle_ms * load_pct / 100;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long elapsed = (now.tv_sec - cycle_start->tv_sec) * 1000 + (now.tv_nsec - cycle_start->tv_nsec) / 1000000;
    if (elapsed < on_ms) return 1;
    /* sleep for remaining time */
    int off_ms = cycle_ms - on_ms;
    if (off_ms > 0) usleep(off_ms * 1000);
    clock_gettime(CLOCK_MONOTONIC, cycle_start);
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "%s", usage); return 1; }

    const char *mode = argv[1];
    int dev_id = atoi(argv[2]);
    int duration = atoi(argv[3]);
    int size_mb = 512;
    if (argc > 4) size_mb = atoi(argv[4]);
    int load_pct = 100;
    if (argc > 5) load_pct = atoi(argv[5]);
    if (load_pct < 1) load_pct = 1;
    if (load_pct > 100) load_pct = 100;
    if (size_mb <= 0) size_mb = 512;

    aclError ret = aclInit(NULL);
    if (ret != ACL_SUCCESS) { fprintf(stderr, "aclInit fail: %d\n", ret); return 1; }

    ret = aclrtSetDevice(dev_id);
    if (ret != ACL_SUCCESS) { fprintf(stderr, "aclrtSetDevice(%d) fail: %d\n", dev_id, ret); aclFinalize(); return 1; }
    aclnnInit(NULL);

    aclrtStream stream;
    aclrtCreateStream(&stream);
    struct timespec cycle_start;
    clock_gettime(CLOCK_MONOTONIC, &cycle_start);

    if (strcmp(mode, "hbm") == 0) {
        size_t bytes = (size_t)size_mb * 1024 * 1024;
        void *d_ptr = NULL;
        ret = aclrtMalloc(&d_ptr, bytes, ACL_MEM_MALLOC_HUGE_FIRST);
        if (ret != ACL_SUCCESS) { fprintf(stderr, "aclrtMalloc %dMB fail: %d\n", size_mb, ret); goto fail; }
        aclrtMemset(d_ptr, bytes, 0xAA, bytes);
        printf("HBM stress: %dMB on dev %d %s\n", size_mb, dev_id, duration > 0 ? "" : "forever");
        if (duration > 0) { sleep(duration); } else { while (1) pause(); }
        aclrtFree(d_ptr);

    } else if (strcmp(mode, "aicore") == 0) {
        /* FP32 GEMM: C = alpha * A * B + beta * C → Cube units */
        int M = 1024, N = 1024, K = 1024;
        size_t a_sz = (size_t)M * K * 4, b_sz = (size_t)K * N * 4, c_sz = (size_t)M * N * 4;
        void *dA = NULL, *dB = NULL, *dC = NULL;
        if (aclrtMalloc(&dA, a_sz, ACL_MEM_MALLOC_HUGE_FIRST) || aclrtMalloc(&dB, b_sz, ACL_MEM_MALLOC_HUGE_FIRST) || aclrtMalloc(&dC, c_sz, ACL_MEM_MALLOC_HUGE_FIRST)) {
            fprintf(stderr, "GEMM malloc fail on dev %d\n", dev_id); goto fail;
        }
        aclrtMemset(dA, a_sz, 0x01, a_sz); aclrtMemset(dB, b_sz, 0x02, b_sz);
        float alpha = 1.0f, beta = 0.0f;
        printf("AICore stress: GEMM %dx%dx%d on dev %d load=%d%% %s\n", M, N, K, dev_id, load_pct, duration > 0 ? "" : "forever");
        int iter = 0;
        if (duration > 0) {
            time_t end = time(NULL) + duration;
            while (time(NULL) < end) {
                aclblasGemmEx(ACL_TRANS_N, ACL_TRANS_N, ACL_TRANS_N, M, N, K,
                              &alpha, dA, M, ACL_FLOAT, dB, K, ACL_FLOAT,
                              &beta, dC, M, ACL_FLOAT, ACL_COMPUTE_HIGH_PRECISION, stream);
                aclrtSynchronizeStream(stream);
                iter++;
                if (load_pct < 100) in_compute_phase(load_pct, &cycle_start);
            }
        } else {
            while (1) {
                aclblasGemmEx(ACL_TRANS_N, ACL_TRANS_N, ACL_TRANS_N, M, N, K,
                              &alpha, dA, M, ACL_FLOAT, dB, K, ACL_FLOAT,
                              &beta, dC, M, ACL_FLOAT, ACL_COMPUTE_HIGH_PRECISION, stream);
                aclrtSynchronizeStream(stream);
                iter++;
                if (load_pct < 100) in_compute_phase(load_pct, &cycle_start);
            }
        }
        aclrtFree(dA); aclrtFree(dB); aclrtFree(dC);

    } else if (strcmp(mode, "aivector") == 0) {
        /* FP16 element-wise Exp: out = exp(self) → Vector units (compute-intensive) */
        int64_t count = 16777216;  /* 16M elements = 32MB (FP16) */
        int64_t dims[2] = {1, count};
        int64_t stride[2] = {count, 1};
        size_t bytes = (size_t)count * 2;
        void *dA = NULL, *dC = NULL;
        if (aclrtMalloc(&dA, bytes, ACL_MEM_MALLOC_HUGE_FIRST) || aclrtMalloc(&dC, bytes, ACL_MEM_MALLOC_HUGE_FIRST)) {
            fprintf(stderr, "Exp malloc fail on dev %d\n", dev_id); goto fail;
        }
        aclrtMemset(dA, bytes, 0x3C, bytes);  /* FP16 ~1.0 */
        aclTensor *tA = aclCreateTensor(dims, 2, ACL_FLOAT16, stride, 0, ACL_FORMAT_ND, dims, 2, dA);
        aclTensor *tC = aclCreateTensor(dims, 2, ACL_FLOAT16, stride, 0, ACL_FORMAT_ND, dims, 2, dC);

        uint64_t ws_size = 0;
        aclOpExecutor *exec = NULL;
        aclnnStatus s1 = aclnnExpGetWorkspaceSize(tA, tC, &ws_size, &exec);
        if (s1 != 0 || exec == NULL) {
            fprintf(stderr, "aclnnExpGetWorkspaceSize fail: status=%d exec=%p\n", s1, (void*)exec);
            goto fail;
        }
        void *ws = NULL;
        if (ws_size > 0) aclrtMalloc(&ws, ws_size, ACL_MEM_MALLOC_HUGE_FIRST);
        printf("AIVector stress: FP16 Exp %ldM elem on dev %d load=%d%% ws=%lu %s\n",
               count / 1048576, dev_id, load_pct, (unsigned long)ws_size, duration > 0 ? "" : "forever");

        int iter = 0;
        if (duration > 0) {
            time_t end = time(NULL) + duration;
            while (time(NULL) < end) {
                for (int i = 0; i < 100; i++) {
                    aclnnExpGetWorkspaceSize(tA, tC, &ws_size, &exec);
                    aclnnExp(ws, ws_size, exec, stream);
                    iter++;
                }
                aclrtSynchronizeStream(stream);
                if (load_pct < 100) in_compute_phase(load_pct, &cycle_start);
            }
            printf("AIVector stress done: %d iterations\n", iter);
        } else {
            while (1) {
                for (int i = 0; i < 100; i++) {
                    aclnnExpGetWorkspaceSize(tA, tC, &ws_size, &exec);
                    aclnnExp(ws, ws_size, exec, stream);
                    iter++;
                }
                aclrtSynchronizeStream(stream);
                if (load_pct < 100) in_compute_phase(load_pct, &cycle_start);
            }
        }
        if (ws) aclrtFree(ws);
        aclDestroyTensor(tA); aclDestroyTensor(tC);
        aclrtFree(dA); aclrtFree(dC);

    } else {
        fprintf(stderr, "unknown mode: %s\n%s", mode, usage);
        goto fail;
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
