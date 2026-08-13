/*
 * _npu_stress.c — NPU stress tool using ACL BLAS (no torch_npu required).
 * Usage: _npu_stress <hbm|aicore|aivector> <device_id> <duration_sec> [size_mb]
 *   hbm:      allocate HBM memory + memset (HBM bandwidth stress)
 *   aicore:   FP32 GEMM loop (Cube compute unit stress)
 *   aivector: FP16 GEMV loop (Vector compute unit stress)
 * duration 0 = run forever (until killed)
 * Monitor: npu-smi info -t usages -i <card> -c 0
 */
#include "acl/acl.h"
#include "acl/ops/acl_cblas.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

static const char *usage = "Usage: _npu_stress <hbm|aicore|aivector> <device_id> <duration_sec> [size_mb]\n"
                           "  duration 0 = run forever (until killed)\n";

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "%s", usage); return 1; }

    const char *mode = argv[1];
    int dev_id = atoi(argv[2]);
    int duration = atoi(argv[3]);
    int size_mb = 512;
    if (argc > 4) size_mb = atoi(argv[4]);

    if (size_mb <= 0) size_mb = 512;

    aclError ret = aclInit(NULL);
    if (ret != ACL_SUCCESS) { fprintf(stderr, "aclInit fail: %d\n", ret); return 1; }

    ret = aclrtSetDevice(dev_id);
    if (ret != ACL_SUCCESS) { fprintf(stderr, "aclrtSetDevice(%d) fail: %d\n", dev_id, ret); aclFinalize(); return 1; }

    aclrtStream stream;
    aclrtCreateStream(&stream);

    if (strcmp(mode, "hbm") == 0) {
        size_t bytes = (size_t)size_mb * 1024 * 1024;
        void *d_ptr = NULL;
        ret = aclrtMalloc(&d_ptr, bytes, ACL_MEM_MALLOC_HUGE_FIRST);
        if (ret != ACL_SUCCESS) { fprintf(stderr, "aclrtMalloc %dMB fail: %d\n", size_mb, ret); goto fail; }
        aclrtMemset(d_ptr, bytes, 0xAA, bytes);
        printf("HBM stress: %dMB allocated+filled on dev %d, holding %s\n", size_mb, dev_id, duration > 0 ? "" : "forever");
        if (duration > 0) { sleep(duration); } else { while (1) pause(); }
        aclrtFree(d_ptr);
        printf("HBM stress done\n");

    } else if (strcmp(mode, "aicore") == 0) {
        /* FP32 GEMM: C(M,N) = alpha * A(M,K) * B(K,N) + beta * C(M,N)
         * Cube compute unit stress → AICore Usage Rate increases */
        int M = 1024, N = 1024, K = 1024;
        size_t a_bytes = (size_t)M * K * 4;  /* FP32 */
        size_t b_bytes = (size_t)K * N * 4;
        size_t c_bytes = (size_t)M * N * 4;
        void *d_A = NULL, *d_B = NULL, *d_C = NULL;
        ret = aclrtMalloc(&d_A, a_bytes, ACL_MEM_MALLOC_HUGE_FIRST);
        if (ret != ACL_SUCCESS) { fprintf(stderr, "malloc A fail: %d\n", ret); goto fail; }
        ret = aclrtMalloc(&d_B, b_bytes, ACL_MEM_MALLOC_HUGE_FIRST);
        if (ret != ACL_SUCCESS) { fprintf(stderr, "malloc B fail: %d\n", ret); aclrtFree(d_A); goto fail; }
        ret = aclrtMalloc(&d_C, c_bytes, ACL_MEM_MALLOC_HUGE_FIRST);
        if (ret != ACL_SUCCESS) { fprintf(stderr, "malloc C fail: %d\n", ret); aclrtFree(d_A); aclrtFree(d_B); goto fail; }
        aclrtMemset(d_A, a_bytes, 0x01, a_bytes);
        aclrtMemset(d_B, b_bytes, 0x02, b_bytes);
        aclrtMemset(d_C, c_bytes, 0x00, c_bytes);
        float alpha = 1.0f, beta = 0.0f;

        printf("AICore stress: FP32 GEMM %dx%dx%d on dev %d %s\n", M, N, K, dev_id, duration > 0 ? "" : "forever");
        int iter = 0;
        if (duration > 0) {
            time_t end = time(NULL) + duration;
            while (time(NULL) < end) {
                aclblasGemmEx(ACL_TRANS_N, ACL_TRANS_N, ACL_TRANS_N,
                              M, N, K,
                              &alpha, d_A, M, ACL_FLOAT,
                              d_B, K, ACL_FLOAT,
                              &beta, d_C, M, ACL_FLOAT,
                              ACL_COMPUTE_HIGH_PRECISION, stream);
                aclrtSynchronizeStream(stream);
                iter++;
            }
        } else {
            while (1) {
                aclblasGemmEx(ACL_TRANS_N, ACL_TRANS_N, ACL_TRANS_N,
                              M, N, K,
                              &alpha, d_A, M, ACL_FLOAT,
                              d_B, K, ACL_FLOAT,
                              &beta, d_C, M, ACL_FLOAT,
                              ACL_COMPUTE_HIGH_PRECISION, stream);
                aclrtSynchronizeStream(stream);
                iter++;
            }
        }
        printf("AICore stress done: %d GEMM iterations\n", iter);
        aclrtFree(d_A); aclrtFree(d_B); aclrtFree(d_C);

    } else if (strcmp(mode, "aivector") == 0) {
        /* FP16 GEMV: y(M) = alpha * A(M,N) * x(N) + beta * y(M)
         * Vector compute unit stress → AIVector Usage Rate increases */
        int M = 4096, N = 4096;
        size_t a_bytes = (size_t)M * N * 2;  /* FP16 */
        size_t x_bytes = (size_t)N * 2;
        size_t y_bytes = (size_t)M * 2;
        void *d_A = NULL, *d_x = NULL, *d_y = NULL;
        ret = aclrtMalloc(&d_A, a_bytes, ACL_MEM_MALLOC_HUGE_FIRST);
        if (ret != ACL_SUCCESS) { fprintf(stderr, "malloc A fail: %d\n", ret); goto fail; }
        ret = aclrtMalloc(&d_x, x_bytes, ACL_MEM_MALLOC_HUGE_FIRST);
        if (ret != ACL_SUCCESS) { fprintf(stderr, "malloc x fail: %d\n", ret); aclrtFree(d_A); goto fail; }
        ret = aclrtMalloc(&d_y, y_bytes, ACL_MEM_MALLOC_HUGE_FIRST);
        if (ret != ACL_SUCCESS) { fprintf(stderr, "malloc y fail: %d\n", ret); aclrtFree(d_A); aclrtFree(d_x); goto fail; }
        aclrtMemset(d_A, a_bytes, 0x01, a_bytes);
        aclrtMemset(d_x, x_bytes, 0x02, x_bytes);
        aclrtMemset(d_y, y_bytes, 0x00, y_bytes);
        aclFloat16 alpha = aclFloatToFloat16(1.0f);
        aclFloat16 beta  = aclFloatToFloat16(0.0f);

        printf("AIVector stress: FP16 GEMV %dx%d on dev %d %s\n", M, N, dev_id, duration > 0 ? "" : "forever");
        int iter = 0;
        if (duration > 0) {
            time_t end = time(NULL) + duration;
            while (time(NULL) < end) {
                aclblasHgemv(ACL_TRANS_N, M, N,
                             &alpha, d_A, M, d_x, 1,
                             &beta, d_y, 1,
                             ACL_COMPUTE_HIGH_PRECISION, stream);
                aclrtSynchronizeStream(stream);
                iter++;
            }
        } else {
            while (1) {
                aclblasHgemv(ACL_TRANS_N, M, N,
                             &alpha, d_A, M, d_x, 1,
                             &beta, d_y, 1,
                             ACL_COMPUTE_HIGH_PRECISION, stream);
                aclrtSynchronizeStream(stream);
                iter++;
            }
        }
        printf("AIVector stress done: %d GEMV iterations\n", iter);
        aclrtFree(d_A); aclrtFree(d_x); aclrtFree(d_y);

    } else {
        fprintf(stderr, "unknown mode: %s\n%s", mode, usage);
        goto fail;
    }

    aclrtDestroyStream(stream);
    aclrtResetDevice(dev_id);
    aclFinalize();
    return 0;

fail:
    aclrtDestroyStream(stream);
    aclrtResetDevice(dev_id);
    aclFinalize();
    return 1;
}
