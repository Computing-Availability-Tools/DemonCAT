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
PKG_INVOKE=""
if command -v apt-get >/dev/null 2>&1; then
    PKG="apt"
    PKG_INVOKE="sudo apt-get"
elif command -v yum >/dev/null 2>&1; then
    PKG="yum"
    PKG_INVOKE="sudo yum"
elif command -v dnf >/dev/null 2>&1; then
    PKG="dnf"
    PKG_INVOKE="sudo dnf"
else
    echo "ERROR: 未识别的包管理器（支持 apt / yum / dnf）"
    exit 1
fi
echo "检测到包管理器: $PKG"
echo ""

# ---- 修复 yum/dnf 的 Python 环境问题 ----
# 场景：conda/miniconda/pyenv 可能把 /usr/bin/python3 指向了非系统 Python，
# 导致 yum/dnf（依赖系统 Python 的 dnf 模块）报 ModuleNotFoundError: No module named 'dnf'
# 这里自动检测并修复。
if [ "$PKG" = "yum" ] || [ "$PKG" = "dnf" ]; then
    if ! $PKG_INVOKE --version >/dev/null 2>&1; then
        # 扫描所有系统 Python 版本，找能导入 dnf 模块的那个
        SYS_PYTHON=""
        for py in /usr/bin/python3.*; do
            [ -x "$py" ] && "$py" -c "import dnf" >/dev/null 2>&1 && SYS_PYTHON="$py" && break
        done
        DNF_BIN=$(command -v dnf-3 2>/dev/null || command -v dnf 2>/dev/null)
        if [ -n "$SYS_PYTHON" ] && [ -n "$DNF_BIN" ]; then
            PKG_INVOKE="sudo $SYS_PYTHON $DNF_BIN"
        else
            echo "-------------------------------------------------------------"
            echo "错误: 包管理器 ($PKG) 无法正常运行。"
            echo "                                                            "
            echo "可能原因: /usr/bin/python3 被非系统 Python 占用（如 conda），"
            echo "且未找到可替代的系统 Python。"
            echo "                                                            "
            echo "手动修复:                                                     "
            echo "  # 查看可用的系统 Python 版本                                "
            echo "  ls /usr/bin/python3.*                                       "
            echo "                                                            "
            echo "  # 找出有 dnf 模块的那个                                     "
            echo "  for py in /usr/bin/python3.*; do                            "
            echo "    \$py -c 'import dnf' && echo \$py 可用                     "
            echo "  done                                                        "
            echo "                                                            "
            echo "  # 修复软链接后重试                                          "
            echo "  ln -sf /usr/bin/python3.X /usr/bin/python3  # X=可用版本    "
            echo "  bash \$0                                                    "
            echo "-------------------------------------------------------------"
            exit 1
        fi
    fi
fi

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
    $PKG_INVOKE install -y $BUILD_PKGS_YUM
    echo ""
    echo "[2/2] 安装运行时依赖..."
    $PKG_INVOKE install -y $RUNTIME_PKGS_YUM
fi

# ---- 检查 NPU 工具 ----
echo ""
echo "=========================================="
echo "依赖安装完成。检查工具可用性："
echo "=========================================="
TOOLS="perl taskset dd tc ip ethtool iptables systemctl python3 hccn_tool"
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
