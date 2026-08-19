/*
 * _npu_stress.c — NPU stress tool using aclnn operators (no torch_npu required).
 * Usage: _npu_stress <hbm|aicore|aivector> <device_id> <duration_sec> [size_mb] [load_pct] [card_id] [chip_id]
 *   hbm:      allocate HBM memory + memset (HBM bandwidth stress)
 *   aicore:   FP16 Matmul loop (Cube compute unit stress → AICore Usage)
 *   aivector: FP16 Exp loop (Vector compute unit stress → AIVector Usage)
 * duration 0 = run forever (until killed)
 * load_pct 1-100 (default 100), duty-cycle: compute for X ms, sleep for Y ms
 * When load_pct < 100 and card_id/chip_id provided, auto-calibrates max_achievable
 * by sampling npu-smi during a 3-second warmup at 100% duty.
 * Monitor: npu-smi info -t usages -i <card> -c 0
 */
#include "acl/acl.h"
#include "aclnn/aclnn_base.h"
#include "aclnn/acl_meta.h"
#include "aclnnop/aclnn_matmul.h"
#include "aclnnop/aclnn_exp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

static const char *usage = "Usage: _npu_stress <hbm|aicore|aivector> <device_id> <duration_sec> [size_mb] [load_pct] [card_id] [chip_id]\n"
                           "  duration 0 = run forever (until killed)\n"
                           "  load_pct 1-100 (default 100)\n"
                           "  card_id/chip_id: for auto-calibration (npu-smi sampling)\n";

/* Sample NPU utilization via npu-smi. Returns 0-100, or -1 on failure. */
static float sample_npu_usage(int card_id, int chip_id, const char *keyword, const char *keyword2) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "npu-smi info -t usages -i %d -c %d 2>/dev/null", card_id, chip_id);
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;
    char line[256];
    float pct = -1;
    while (fgets(line, sizeof(line), fp)) {
        if ((keyword && strstr(line, keyword)) || (keyword2 && strstr(line, keyword2))) {
            char *p = strrchr(line, ':');
            if (p) {
                p++;
                while (*p == ' ' || *p == '\t') p++;
                pct = atof(p);
            }
            break;
        }
    }
    pclose(fp);
    return pct;
}

/* Calibrate peak achievable utilization by running at 100% duty for 3 seconds
 * and sampling npu-smi. Returns 0.1-1.0 (fraction of peak). */
static float calibrate_peak(int card_id, int chip_id, const char *kw1, const char *kw2,
                            float fallback) {
    if (card_id < 0 || chip_id < 0) return fallback;
    float peak = 0;
    int samples = 0;
    time_t cal_end = time(NULL) + 3;
    while (time(NULL) < cal_end) {
        usleep(300000);
        float v = sample_npu_usage(card_id, chip_id, kw1, kw2);
        if (v > 0) {
            if (v > peak) peak = v;
            samples++;
        }
    }
    if (samples == 0 || peak < 1) return fallback;
    peak /= 100.0f;
    if (peak > 1.0f) peak = 1.0f;
    if (peak < 0.1f) peak = fallback;
    fprintf(stderr, "calibrated max_achievable=%.0f%% (%d samples)\n", peak * 100, samples);
    return peak;
}

/* After sync, measure actual compute time and sleep proportionally.
 * max_achievable is auto-calibrated per-chip. */
static void pace_load(int load_pct, struct timespec *batch_start, float max_achievable) {
    if (load_pct >= 100) return;
    int duty = (int)(load_pct / max_achievable);
    if (duty < 1) duty = 1;
    if (duty > 100) duty = 100;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long compute_us = (now.tv_sec - batch_start->tv_sec) * 1000000 + (now.tv_nsec - batch_start->tv_nsec) / 1000;
    if (compute_us <= 0) compute_us = 1;
    long off_us = compute_us * (100 - duty) / duty;
    if (off_us > 0) usleep(off_us);
    clock_gettime(CLOCK_MONOTONIC, batch_start);
}

/* helper: create a 2D FP16 tensor backed by device memory */
static aclTensor* make_tensor(void *dev_ptr, int rows, int cols) {
    int64_t dims[2] = {rows, cols};
    int64_t stride[2] = {cols, 1};
    return aclCreateTensor(dims, 2, ACL_FLOAT16, stride, 0, ACL_FORMAT_ND, dims, 2, dev_ptr);
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
    int card_id = -1, chip_id = -1;
    if (argc > 6) card_id = atoi(argv[6]);
    if (argc > 7) chip_id = atoi(argv[7]);
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
    struct timespec batch_start;
    clock_gettime(CLOCK_MONOTONIC, &batch_start);

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
        /* FP16 Matmul: C = A(M,K) * B(K,N) → Cube units → AICore Usage
         * 8192 size saturates Cube array on both 910B4 and 910C. */
        int M = 8192, K = 8192, N = 8192;
        size_t szA = (size_t)M*K*2, szB = (size_t)K*N*2, szC = (size_t)M*N*2;
        void *dA=NULL, *dB=NULL, *dC=NULL;
        if (aclrtMalloc(&dA, szA, ACL_MEM_MALLOC_HUGE_FIRST) ||
            aclrtMalloc(&dB, szB, ACL_MEM_MALLOC_HUGE_FIRST) ||
            aclrtMalloc(&dC, szC, ACL_MEM_MALLOC_HUGE_FIRST)) {
            fprintf(stderr, "matmul malloc fail on dev %d\n", dev_id); goto fail;
        }
        aclrtMemset(dA, szA, 0x00, szA); aclrtMemset(dB, szB, 0x00, szB);
        aclTensor *tA = make_tensor(dA, M, K);
        aclTensor *tB = make_tensor(dB, K, N);
        aclTensor *tC = make_tensor(dC, M, N);

        uint64_t ws_size = 0;
        aclOpExecutor *exec = NULL;
        aclnnStatus s = aclnnMatmulGetWorkspaceSize(tA, tB, tC, 0, &ws_size, &exec);
        if (s != 0 || !exec) { fprintf(stderr, "aclnnMatmulGetWorkspaceSize fail: %d\n", s); goto fail; }
        void *ws = NULL;
        if (ws_size > 0) aclrtMalloc(&ws, ws_size, ACL_MEM_MALLOC_HUGE_FIRST);
        printf("AICore stress: FP16 Matmul %dx%dx%d on dev %d load=%d%% %s\n", M, N, K, dev_id, load_pct, duration > 0 ? "" : "forever");

        /* Auto-calibrate peak if load_pct < 100 and card/chip provided */
        float max_achievable = 0.90f;
        if (load_pct < 100) {
            max_achievable = calibrate_peak(card_id, chip_id, "Aicore", "Aicube", 0.90f);
        }

        int iter = 0;
        if (duration > 0) {
            time_t end = time(NULL) + duration;
            while (time(NULL) < end) {
                for (int i = 0; i < 100; i++) {
                    aclnnMatmulGetWorkspaceSize(tA, tB, tC, 0, &ws_size, &exec);
                    aclnnMatmul(ws, ws_size, exec, stream);
                    iter++;
                }
                aclrtSynchronizeStream(stream);
                if (load_pct < 100) pace_load(load_pct, &batch_start, max_achievable);
            }
        } else {
            while (1) {
                for (int i = 0; i < 100; i++) {
                    aclnnMatmulGetWorkspaceSize(tA, tB, tC, 0, &ws_size, &exec);
                    aclnnMatmul(ws, ws_size, exec, stream);
                    iter++;
                }
                aclrtSynchronizeStream(stream);
                if (load_pct < 100) pace_load(load_pct, &batch_start, max_achievable);
            }
        }
        if (ws) aclrtFree(ws);
        aclDestroyTensor(tA); aclDestroyTensor(tB); aclDestroyTensor(tC);
        aclrtFree(dA); aclrtFree(dB); aclrtFree(dC);

    } else if (strcmp(mode, "aivector") == 0) {
        /* FP16 Exp: out = exp(self) → Vector units → AIVector Usage
         * 256M elements to saturate Vector units on both 910B4 and 910C. */
        int64_t count = 268435456;  /* 256M elements = 512MB FP16 */
        size_t bytes = (size_t)count * 2;
        void *dA=NULL, *dC=NULL;
        if (aclrtMalloc(&dA, bytes, ACL_MEM_MALLOC_HUGE_FIRST) ||
            aclrtMalloc(&dC, bytes, ACL_MEM_MALLOC_HUGE_FIRST)) {
            fprintf(stderr, "exp malloc fail on dev %d\n", dev_id); goto fail;
        }
        aclrtMemset(dA, bytes, 0x3C, bytes);  /* FP16 ~1.0 */
        int64_t dims[2] = {1, count};
        int64_t stride[2] = {count, 1};
        aclTensor *tA = aclCreateTensor(dims, 2, ACL_FLOAT16, stride, 0, ACL_FORMAT_ND, dims, 2, dA);
        aclTensor *tC = aclCreateTensor(dims, 2, ACL_FLOAT16, stride, 0, ACL_FORMAT_ND, dims, 2, dC);

        uint64_t ws_size = 0;
        aclOpExecutor *exec = NULL;
        aclnnStatus s = aclnnExpGetWorkspaceSize(tA, tC, &ws_size, &exec);
        if (s != 0 || !exec) { fprintf(stderr, "aclnnExpGetWorkspaceSize fail: %d\n", s); goto fail; }
        void *ws = NULL;
        if (ws_size > 0) aclrtMalloc(&ws, ws_size, ACL_MEM_MALLOC_HUGE_FIRST);
        printf("AIVector stress: FP16 Exp %ldM elem on dev %d load=%d%% %s\n",
               count / 1048576, dev_id, load_pct, duration > 0 ? "" : "forever");

        /* Auto-calibrate peak if load_pct < 100 and card/chip provided */
        float max_achievable = 0.92f;
        if (load_pct < 100) {
            max_achievable = calibrate_peak(card_id, chip_id, "Aivector", NULL, 0.92f);
        }

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
                if (load_pct < 100) pace_load(load_pct, &batch_start, max_achievable);
            }
        } else {
            while (1) {
                for (int i = 0; i < 100; i++) {
                    aclnnExpGetWorkspaceSize(tA, tC, &ws_size, &exec);
                    aclnnExp(ws, ws_size, exec, stream);
                    iter++;
                }
                aclrtSynchronizeStream(stream);
                if (load_pct < 100) pace_load(load_pct, &batch_start, max_achievable);
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
