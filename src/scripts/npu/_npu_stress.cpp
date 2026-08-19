/*
 * _npu_stress.cpp — NPU stress tool using aclnnAdd (no torch_npu required).
 * Usage: _npu_stress <aicore|aicpu|aivector|hbm> <dev_id> [size] [load_pct] [duration_sec]
 *   aicore:   aclnnAdd loop (FP16, 8192 base — stress NPU compute)
 *   aicpu:    aclnnAdd loop (FP16, 2000 base — smaller workload)
 *   aivector: aclnnAdd loop (FP16, 8192 base — stress NPU compute)
 *   hbm:      aclrtMalloc + memset (HBM memory stress)
 * duration 0 = run forever (until killed)
 * load_pct 1-100 (default 100): scales tensor size.
 *   100 = full size, 50 = half size. Continuous compute, no sleeping.
 * Monitor: npu-smi info -t usages -i <card> -c 0
 */
#include "acl/acl.h"
#include "aclnn/aclnn_base.h"
#include "aclnn/acl_meta.h"
#include "aclnnop/aclnn_add.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

static const char *usage = "Usage: _npu_stress <aicore|aicpu|aivector|hbm> <dev_id> [size] [load_pct] [duration_sec]\n"
                           "  duration 0 = run forever (until killed)\n"
                           "  load_pct 1-100 (default 100): scales tensor size\n";

/* helper: create a 2D FP16 tensor backed by device memory */
static aclTensor* make_tensor(void *dev_ptr, int rows, int cols) {
    int64_t dims[2] = {rows, cols};
    int64_t stride[2] = {cols, 1};
    return aclCreateTensor(dims, 2, ACL_FLOAT16, stride, 0, ACL_FORMAT_ND, dims, 2, dev_ptr);
}

/* run aclnnAdd stress loop */
static void run_add_stress(aclrtStream stream, int shape, int duration) {
    size_t bytes = (size_t)shape * shape * 2;  /* FP16 */
    void *dA=NULL, *dB=NULL, *dC=NULL;
    if (aclrtMalloc(&dA, bytes, ACL_MEM_MALLOC_HUGE_FIRST) ||
        aclrtMalloc(&dB, bytes, ACL_MEM_MALLOC_HUGE_FIRST) ||
        aclrtMalloc(&dC, bytes, ACL_MEM_MALLOC_HUGE_FIRST)) {
        fprintf(stderr, "add malloc fail (shape=%d)\n", shape);
        return;
    }
    aclrtMemset(dA, bytes, 0x3C, bytes);
    aclrtMemset(dB, bytes, 0x3C, bytes);
    aclTensor *tA = make_tensor(dA, shape, shape);
    aclTensor *tB = make_tensor(dB, shape, shape);
    aclTensor *tC = make_tensor(dC, shape, shape);

    float alpha_val = 1.0f;
    aclScalar *alpha = aclCreateScalar(&alpha_val, ACL_FLOAT);

    uint64_t ws_size = 0;
    aclOpExecutor *exec = NULL;
    aclnnStatus s = aclnnAddGetWorkspaceSize(tA, tB, alpha, tC, &ws_size, &exec);
    if (s != 0 || !exec) {
        fprintf(stderr, "aclnnAddGetWorkspaceSize fail: %d\n", s);
        aclDestroyScalar(alpha);
        aclDestroyTensor(tA); aclDestroyTensor(tB); aclDestroyTensor(tC);
        aclrtFree(dA); aclrtFree(dB); aclrtFree(dC);
        return;
    }
    void *ws = NULL;
    if (ws_size > 0) aclrtMalloc(&ws, ws_size, ACL_MEM_MALLOC_HUGE_FIRST);
    printf("Add stress: %dx%d FP16 on stream, duration=%s\n", shape, shape, duration > 0 ? "" : "forever");
    fflush(stdout);

    if (duration > 0) {
        time_t end = time(NULL) + duration;
        while (time(NULL) < end) {
            for (int i = 0; i < 100; i++) {
                aclnnAddGetWorkspaceSize(tA, tB, alpha, tC, &ws_size, &exec);
                aclnnAdd(ws, ws_size, exec, stream);
            }
            aclrtSynchronizeStream(stream);
        }
    } else {
        while (1) {
            for (int i = 0; i < 100; i++) {
                aclnnAddGetWorkspaceSize(tA, tB, alpha, tC, &ws_size, &exec);
                aclnnAdd(ws, ws_size, exec, stream);
            }
            aclrtSynchronizeStream(stream);
        }
    }
    if (ws) aclrtFree(ws);
    aclDestroyScalar(alpha);
    aclDestroyTensor(tA); aclDestroyTensor(tB); aclDestroyTensor(tC);
    aclrtFree(dA); aclrtFree(dB); aclrtFree(dC);
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
        if (strcmp(mode, "aicore") == 0)       base_shape = 8192;
        else if (strcmp(mode, "aicpu") == 0)  base_shape = 2000;
        else if (strcmp(mode, "aivector") == 0) base_shape = 8192;
        else { fprintf(stderr, "unknown mode: %s\n%s", mode, usage); goto fail; }

        int shape = size > 0 ? size : (256 > base_shape * load_pct / 100 ? 256 : base_shape * load_pct / 100);
        if (shape < 256) shape = 256;
        run_add_stress(stream, shape, duration);
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
