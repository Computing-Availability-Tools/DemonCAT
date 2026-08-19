#!/usr/bin/env python3
"""_npu_stress.py — NPU stress tool using torch_npu (replaces C++ ACL version).

Usage: _npu_stress.py <aicore|aivector|hbm> <dev_id> [size] [load_pct] [duration_sec]
  aicore:   torch.matmul (Cube units → AICore Usage)
  aivector: torch.add (Vector units → AIVector Usage)
  hbm:      allocate N tensors (HBM memory stress)
  load_pct 1-100 (default 100): scales tensor size, NOT duty-cycle.
    100 = full 5120 (saturates chip), 50 = 2560 (half the compute)
    Continuous compute, no sleeping → npu-smi shows stable utilization.
  duration 0 = run forever (until killed)

Based on reference implementation at rNPU_OS/bin/stress/static_call_main.py.
"""
import sys
import os
import time
import copy
import signal

def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <aicore|aivector|hbm> <dev_id> [size] [load_pct] [duration_sec]", file=sys.stderr)
        sys.exit(1)

    mode = sys.argv[1]
    dev_id = int(sys.argv[2])
    size = 0
    if len(sys.argv) > 3:
        size = int(sys.argv[3])
    load_pct = 100
    if len(sys.argv) > 4:
        load_pct = int(sys.argv[4])
    duration = 0
    if len(sys.argv) > 5:
        duration = int(sys.argv[5])
    if load_pct < 1: load_pct = 1
    if load_pct > 100: load_pct = 100

    import torch
    import torch_npu

    if not torch_npu.npu.is_available():
        print("ERROR: NPU not available", file=sys.stderr)
        sys.exit(1)

    device = torch.device(f"npu:{dev_id}")

    if mode == "aicore":
        base_shape = 5120
        shape = size if size > 0 else max(256, int(base_shape * load_pct / 100))
        mat = torch.randn((shape, shape), dtype=torch.float32).to(device)
        print(f"AICore stress: matmul {shape}x{shape} FP32 on dev {dev_id} load={load_pct}% {'forever' if duration == 0 else str(duration) + 's'}")
        sys.stdout.flush()
        if duration > 0:
            t0 = time.time()
            while time.time() - t0 < duration:
                torch.matmul(mat, mat)
        else:
            while True:
                torch.matmul(mat, mat)

    elif mode == "aivector":
        base_shape = 8500
        shape = size if size > 0 else max(256, int(base_shape * load_pct / 100))
        mat = torch.randn((shape, shape), dtype=torch.float32).to(device)
        print(f"AIVector stress: add {shape}x{shape} FP32 on dev {dev_id} load={load_pct}% {'forever' if duration == 0 else str(duration) + 's'}")
        sys.stdout.flush()
        if duration > 0:
            t0 = time.time()
            while time.time() - t0 < duration:
                torch.add(mat, mat)
        else:
            while True:
                torch.add(mat, mat)

    elif mode == "hbm":
        shape = 5120
        unit_mb = shape * shape * 4 // (1024 * 1024)
        if size > 0:
            num_tensors = max(1, size // unit_mb)
        else:
            num_tensors = 60
        mat = torch.randn((shape, shape), dtype=torch.float32).to(device)
        data_list = []
        for _ in range(num_tensors):
            data_list.append(copy.deepcopy(mat))
        print(f"HBM stress: {num_tensors} x {unit_mb}MB = {num_tensors * unit_mb}MB on dev {dev_id} {'forever' if duration == 0 else str(duration) + 's'}")
        sys.stdout.flush()
        if duration > 0:
            time.sleep(duration)
        else:
            while True:
                time.sleep(3600)
        del data_list

    else:
        print(f"unknown mode: {mode}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
