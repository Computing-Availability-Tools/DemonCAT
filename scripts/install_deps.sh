#!/bin/bash
# scripts/install_deps.sh — 一键安装 DemonCAT 编译 + 运行时依赖
# 支持 Debian/Ubuntu (apt) 和 RHEL/CentOS (yum/dnf)
# 用法: bash scripts/install_deps.sh
set -e

echo "=========================================="
echo "DemonCAT 依赖安装脚本"
echo "=========================================="

# ---- 检测包管理器 ----
PKG=""
if command -v apt-get >/dev/null 2>&1; then
    PKG="apt"
elif command -v yum >/dev/null 2>&1; then
    PKG="yum"
elif command -v dnf >/dev/null 2>&1; then
    PKG="dnf"
else
    echo "ERROR: 未识别的包管理器（支持 apt / yum / dnf）"
    exit 1
fi
echo "检测到包管理器: $PKG"
echo ""

# ---- 依赖列表 ----
# 编译依赖
BUILD_PKGS_APT="cmake gcc make libc6-dev"
BUILD_PKGS_YUM="cmake gcc make glibc-devel"

# 运行时依赖（按模块）
RUNTIME_PKGS_APT="iproute2 ethtool iptables perl python3 util-linux coreutils"
RUNTIME_PKGS_YUM="iproute ethtool iptables perl python3 util-linux coreutils"

# ---- 安装 ----
if [ "$PKG" = "apt" ]; then
    echo "[1/2] 安装编译依赖..."
    sudo apt-get update -qq
    sudo apt-get install -y $BUILD_PKGS_APT
    echo ""
    echo "[2/2] 安装运行时依赖..."
    sudo apt-get install -y $RUNTIME_PKGS_APT
else
    # yum / dnf
    echo "[1/2] 安装编译依赖..."
    sudo $PKG install -y $BUILD_PKGS_YUM
    echo ""
    echo "[2/2] 安装运行时依赖..."
    sudo $PKG install -y $RUNTIME_PKGS_YUM
fi

# ---- 检查 NPU 工具 ----
echo ""
echo "=========================================="
echo "依赖安装完成。检查工具可用性："
echo "=========================================="
TOOLS="cmake gcc make perl taskset dd tc ip ethtool iptables systemctl python3 hccn_tool"
for t in $TOOLS; do
    if command -v "$t" >/dev/null 2>&1; then
        echo "  $t: ✅"
    else
        echo "  $t: ❌ (NPU 故障需要 Atlas 硬件驱动，其他模块不受影响)"
    fi
done

echo ""
echo "下一步："
echo "  cmake -B build && cmake --build build"
echo "  ctest --test-dir build --output-on-failure"
echo "  ./build/dcat --help"
