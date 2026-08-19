/*
 * _npu_stress.c — NPU stress tool using aclnn operators (no torch_npu required).
 * Usage: _npu_stress <hbm|aicore|aivector> <device_id> <duration_sec> [size_mb] [load_pct]
 *   hbm:      allocate HBM memory + memset (HBM bandwidth stress)
 *   aicore:   FP16 Matmul loop (Cube compute unit stress → AICore Usage)
 *   aivector: FP16 Exp loop (Vector compute unit stress → AIVector Usage)
 * duration 0 = run forever (until killed)
 * load_pct 1-100 (default 100): scales workload size (NOT duty-cycle).
 *   100 = full 8192 matmul (saturates 910C/910B3 to ~100%)
 *   50  = 4096 matmul (910C: ~50%, 910B4: ~100% since fewer Cube units)
 *   Continuous compute, no sleeping → npu-smi shows stable utilization.
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

static const char *usage = "Usage: _npu_stress <hbm|aicore|aivector> <device_id> <duration_sec> [size_mb] [load_pct]\n"
                           "  duration 0 = run forever (until killed)\n"
                           "  load_pct 1-100 (default 100): scales workload size\n";

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
         * Scale matmul size by load_pct: 100%=8192, 50%=4096, 25%=2048.
         * Bigger matmul → more Cube units occupied → higher utilization.
         * 910C: 8192≈100%, 4096≈50%. 910B4: 8192≈100%, 4096≈100% (fewer units). */
        int M = 8192 * load_pct / 100;
        if (M < 256) M = 256;
        int K = M, N = M;
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

        if (duration > 0) {
            time_t end = time(NULL) + duration;
            while (time(NULL) < end) {
                for (int i = 0; i < 100; i++) {
                    aclnnMatmulGetWorkspaceSize(tA, tB, tC, 0, &ws_size, &exec);
                    aclnnMatmul(ws, ws_size, exec, stream);
                }
                aclrtSynchronizeStream(stream);
            }
        } else {
            while (1) {
                for (int i = 0; i < 100; i++) {
                    aclnnMatmulGetWorkspaceSize(tA, tB, tC, 0, &ws_size, &exec);
                    aclnnMatmul(ws, ws_size, exec, stream);
                }
                aclrtSynchronizeStream(stream);
            }
        }
        if (ws) aclrtFree(ws);
        aclDestroyTensor(tA); aclDestroyTensor(tB); aclDestroyTensor(tC);
        aclrtFree(dA); aclrtFree(dB); aclrtFree(dC);

    } else if (strcmp(mode, "aivector") == 0) {
        /* FP16 Exp: out = exp(self) → Vector units → AIVector Usage
         * Scale element count by load_pct: 100%=256M, 50%=128M.
         * More elements → more Vector units occupied → higher utilization. */
        int64_t count = (int64_t)268435456 * load_pct / 100;
        if (count < 1048576) count = 1048576;  /* min 1M */
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

        if (duration > 0) {
            time_t end = time(NULL) + duration;
            while (time(NULL) < end) {
                for (int i = 0; i < 100; i++) {
                    aclnnExpGetWorkspaceSize(tA, tC, &ws_size, &exec);
                    aclnnExp(ws, ws_size, exec, stream);
                }
                aclrtSynchronizeStream(stream);
            }
        } else {
            while (1) {
                for (int i = 0; i < 100; i++) {
                    aclnnExpGetWorkspaceSize(tA, tC, &ws_size, &exec);
                    aclnnExp(ws, ws_size, exec, stream);
                }
                aclrtSynchronizeStream(stream);
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
