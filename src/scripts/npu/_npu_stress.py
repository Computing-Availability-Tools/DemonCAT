#!/usr/bin/env python3
"""_npu_stress.py — NPU resource stress tool (aicore/aicpu/aivector/hbm).
Requires: pydcmi + torch + torch_npu + Ascend toolkit.
Usage: python3 _npu_stress.py --chip 2 --task aicore --duration 60
       python3 _npu_stress.py --chip 2 --task hbm --hbm_gb 5 --duration 60
"""
import argparse
import copy
import datetime
import os
import signal
import sys
import time

def check_deps():
    missing = []
    try:
        import pydcmi.dcmi_api_v2 as dcmi
    except ImportError:
        missing.append("pydcmi")
    try:
        import torch
    except ImportError:
        missing.append("torch")
    try:
        import torch_npu
    except ImportError:
        missing.append("torch_npu")
    if missing:
        print(f"ERROR: missing dependencies: {', '.join(missing)}", file=sys.stderr)
        print("Install: pip install pydcmi torch torch_npu", file=sys.stderr)
        print("Or: source /usr/local/Ascend/ascend-toolkit/set_env.sh", file=sys.stderr)
        sys.exit(1)

def stress_aicore(device_id, duration, data_shape=5120):
    mat = torch.as_tensor(torch.randn((data_shape, data_shape)), dtype=torch.float32).to(device_id)
    start = datetime.datetime.now()
    while True:
        torch.matmul(mat, mat)
        if (datetime.datetime.now() - start).total_seconds() > duration:
            break

def stress_aivector(device_id, duration, data_shape=8500):
    mat = torch.as_tensor(torch.randn((data_shape, data_shape)), dtype=torch.float32).to(device_id)
    start = datetime.datetime.now()
    while True:
        torch.add(mat, mat)
        if (datetime.datetime.now() - start).total_seconds() > duration:
            break

def stress_hbm(device_id, duration, hbm_gb, data_shape=5120, unit_size=100):
    mat = torch.as_tensor(torch.randn((data_shape, data_shape)), dtype=torch.float32).to(device_id)
    tensor_count = (hbm_gb * 1024) // unit_size
    data_list = []
    for _ in range(tensor_count):
        data_list.append(copy.deepcopy(mat))
    time.sleep(duration)
    del data_list

def main():
    parser = argparse.ArgumentParser(description="NPU stress tool")
    parser.add_argument("--chip", type=int, required=True, help="NPU chip ID")
    parser.add_argument("--task", required=True, choices=["aicore", "aivector", "hbm"], help="stress type")
    parser.add_argument("--duration", type=int, default=60, help="duration in seconds (default: 60)")
    parser.add_argument("--hbm_gb", type=int, default=5, help="HBM to occupy in GB (default: 5)")
    args = parser.parse_args()

    check_deps()
    import pydcmi.dcmi_api_v2 as dcmi
    dcmi.dcmi_init()

    device_id = f"npu:{args.chip}"
    try:
        torch_npu.npu.set_device(device_id)
    except Exception as e:
        print(f"ERROR: cannot set device {device_id}: {e}", file=sys.stderr)
        sys.exit(1)

    print(f"Stress: chip={args.chip} task={args.task} duration={args.duration}s", flush=True)

    if args.task == "aicore":
        stress_aicore(device_id, args.duration)
    elif args.task == "aivector":
        stress_aivector(device_id, args.duration)
    elif args.task == "hbm":
        stress_hbm(device_id, args.duration, args.hbm_gb)

    print("Done.", flush=True)

if __name__ == "__main__":
    main()
