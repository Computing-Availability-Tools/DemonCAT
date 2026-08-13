/*
 * _npu_stress.c — NPU stress tool using ACL (no torch_npu required).
 * Usage: _npu_stress <hbm|aicore|aivector> <device_id> <duration_sec> [size_mb]
 *   hbm:      allocate HBM memory + memset (HBM bandwidth stress)
 *   aicore:   allocate HBM + device-to-device memcpy loop (AICore memory subsystem stress)
 *   aivector: same as aicore (AIVector shares HBM subsystem)
 * Monitor: npu-smi info -t usages -i <card> -c 0
 */
#include "acl/acl.h"
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

    /* duration <= 0 means run forever (until killed) */
    if (size_mb <= 0) size_mb = 512;

    aclError ret = aclInit(NULL);
    if (ret != ACL_SUCCESS) { fprintf(stderr, "aclInit fail: %d\n", ret); return 1; }

    ret = aclrtSetDevice(dev_id);
    if (ret != ACL_SUCCESS) { fprintf(stderr, "aclrtSetDevice(%d) fail: %d\n", dev_id, ret); aclFinalize(); return 1; }

    size_t bytes = (size_t)size_mb * 1024 * 1024;

    if (strcmp(mode, "hbm") == 0) {
        void *d_ptr = NULL;
        ret = aclrtMalloc(&d_ptr, bytes, ACL_MEM_MALLOC_HUGE_FIRST);
        if (ret != ACL_SUCCESS) { fprintf(stderr, "aclrtMalloc %dMB fail: %d\n", size_mb, ret); goto done; }
        aclrtMemset(d_ptr, bytes, 0xAA, bytes);
        printf("HBM stress: %dMB allocated+filled on dev %d, holding %s\n", size_mb, dev_id, duration > 0 ? "" : "forever");
        if (duration > 0) {
            sleep(duration);
        } else {
            while (1) pause();
        }
        aclrtFree(d_ptr);
        printf("HBM stress done\n");
    } else if (strcmp(mode, "aicore") == 0 || strcmp(mode, "aivector") == 0) {
        void *d_src = NULL, *d_dst = NULL;
        ret = aclrtMalloc(&d_src, bytes, ACL_MEM_MALLOC_HUGE_FIRST);
        if (ret != ACL_SUCCESS) { fprintf(stderr, "malloc src fail: %d\n", ret); goto done; }
        ret = aclrtMalloc(&d_dst, bytes, ACL_MEM_MALLOC_HUGE_FIRST);
        if (ret != ACL_SUCCESS) { fprintf(stderr, "malloc dst fail: %d\n", ret); aclrtFree(d_src); goto done; }
        aclrtMemset(d_src, bytes, 0xAA, bytes);
        aclrtMemset(d_dst, bytes, 0x55, bytes);

        aclrtStream stream;
        aclrtCreateStream(&stream);

        printf("%s stress: %dMB x2, d2d memcpy loop on dev %d %s\n",
               mode, size_mb, dev_id, duration > 0 ? "" : "forever");

        int iter = 0;
        if (duration > 0) {
            time_t end = time(NULL) + duration;
            while (time(NULL) < end) {
                aclrtMemcpyAsync(d_dst, bytes, d_src, bytes, ACL_MEMCPY_DEVICE_TO_DEVICE, stream);
                aclrtSynchronizeStream(stream);
                iter++;
            }
        } else {
            while (1) {
                aclrtMemcpyAsync(d_dst, bytes, d_src, bytes, ACL_MEMCPY_DEVICE_TO_DEVICE, stream);
                aclrtSynchronizeStream(stream);
                iter++;
            }
        }
        printf("%s stress done: %d iterations\n", mode, iter);

        aclrtDestroyStream(stream);
        aclrtFree(d_src);
        aclrtFree(d_dst);
    } else {
        fprintf(stderr, "unknown mode: %s\n%s", mode, usage);
        goto done;
    }

done:
    aclrtResetDevice(dev_id);
    aclFinalize();
    return 0;
}
