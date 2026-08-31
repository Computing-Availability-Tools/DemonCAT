# shellcheck shell=bash
# _common.sh — npu module shared helpers. Sourced by rNPU_*.sh scripts.

# _npu_stress binary is linked against ascend-toolkit/lib64 (RPATH),
# so ASCEND_OPP_PATH must be from the same toolkit — not nnae or other CANN.
# Only set if ASCEND_TOOLKIT_HOME is explicitly provided; otherwise let the
# binary's ensure_opp_path() handle validation (it checks op_api existence).
if [ -n "${ASCEND_TOOLKIT_HOME:-}" ] && [ -d "${ASCEND_TOOLKIT_HOME}/opp" ]; then
    export ASCEND_OPP_PATH="${ASCEND_TOOLKIT_HOME}/opp"
fi

DEV_MAP_FILE="/tmp/dcat-npu-dev-map"

npu_check_env() {
    command -v hccn_tool >/dev/null 2>&1 || { echo "hccn_tool not found in PATH" >&2; exit 1; }
}

# hccn_tool command prefix with timeout (prevents hang on offline/unbound chips)
HCCN_TO="timeout 5"

# Uniform hccn_tool invocation with busy-retry. Config writes/reads on a shared
# NPU node often hit "hccn_tool is busy, please try again." (external monitor
# processes concurrently driving hccn_tool). Retry with backoff so a transient
# busy window does not turn a real inject/clean/query into a false failure.
# Usage: hccn() <subcmd> [args...]   (assumes $chip set)
# Returns stdout and exit code of the underlying hccn_tool.
hccn() {
    # 局部变量加 _h_ 前缀：hccn() 会被 npu_foreach_chip/npu_query_noargs
    # 在 eval 中调用，若复用裸 _rc/_out/_n/_sleep 会把调用方的同名全局变量
    # 冲掉 → npu_foreach_chip "全 NOT ACTIVE" 也误报 rc=0 → confirmed 假 true。
    _h_n=0
    _h_sleep=1
    while :; do
        _h_out=$($HCCN_TO hccn_tool -i "$chip" "$@" 2>&1)
        _h_rc=$?
        case "$_h_out" in
            *"is busy"*|*"busy, please"*)
                _h_n=$((_h_n + 1))
                if [ "$_h_n" -ge 6 ]; then
                    echo "$_h_out" >&2
                    return 1
                fi
                sleep "$_h_sleep"
                _h_sleep=$((_h_sleep * 2))
                ;;
            *)
                printf '%s\n' "$_h_out"
                return "$_h_rc"
                ;;
        esac
    done
}

npu_validate_chip() {
    case "$1" in
        ''|*[!0-9]*) return 1 ;;
    esac
    return 0
}

# Generate /tmp/dcat-npu-dev-map if missing.
# Format: <Phy-ID> <ACL-dev-id>
# ACL dev ID = sequential index of Phy-IDs (numerically sorted)
npu_gen_dev_map() {
    [ -f "$DEV_MAP_FILE" ] && return 0
    local acl_id=0
    for phy_id in $(ls /dev/davinci[0-9]* 2>/dev/null | sed 's|/dev/davinci||;s/[^0-9].*//' | sort -n); do
        echo "$phy_id $acl_id" >> "$DEV_MAP_FILE"
        acl_id=$((acl_id + 1))
    done
    [ -f "$DEV_MAP_FILE" ] || return 1
}

# Lookup ACL device ID from Phy-ID (auto-generate map if missing)
npu_acl_dev_id() {
    [ ! -f "$DEV_MAP_FILE" ] && npu_gen_dev_map
    awk -v phy="$1" '$1==phy{print $2; exit}' "$DEV_MAP_FILE" 2>/dev/null
}

# Report a missing ACL dev id with a non-misleading message.
# "dev-map missing?" was wrong when the file exists but the chip is simply not in it.
npu_acl_dev_id_err() {
    if [ -f "$DEV_MAP_FILE" ]; then
        echo "chip $1 not in /tmp/dcat-npu-dev-map (no ACL device for this Phy-ID)" >&2
    else
        echo "ACL dev map /tmp/dcat-npu-dev-map missing (run inject once to generate)" >&2
    fi
}

# Get PCIe BDF from Phy-ID via devdrv driver sysfs.
# devdrv driver's dev_id attribute = Phy-ID (exact mapping, no sorting needed).
npu_phy_to_bdf() {
    local phy_id="$1" bdf
    for bdf in /sys/bus/pci/drivers/devdrv_device_driver/0000:*; do
        [ -e "$bdf" ] || continue
        bdf=$(basename "$bdf")
        if [ "$(cat "/sys/bus/pci/drivers/devdrv_device_driver/$bdf/dev_id" 2>/dev/null)" = "$phy_id" ]; then
            echo "$bdf"
            return 0
        fi
    done
    return 1
}

# Convert Phy-ID to "<npu_card_id> <chip_within_card>" for npu-smi -i -c queries.
# Auto-detects chips-per-card from /dev/davinci count vs unique NPU card count.
npu_phy_to_card() {
    local phy_id="$1" n_davinci n_cards per_card
    n_davinci=$(ls /dev/davinci[0-9]* 2>/dev/null | wc -l)
    # Count unique NPU card IDs from npu-smi info first column (deduplicated)
    n_cards=$(npu-smi info 2>/dev/null | grep -oE '^\| [0-9]+' | awk '{print $2}' | sort -nu | wc -l)
    if [ "$n_davinci" -gt 0 ] && [ "$n_cards" -gt 0 ]; then
        per_card=$((n_davinci / n_cards))
        if [ "$per_card" -ge 1 ]; then
            echo "$((phy_id / per_card)) $((phy_id % per_card))"
            return 0
        fi
    fi
    # Fallback: 1 chip per card (910B4 where Phy-ID = NPU card ID)
    echo "$phy_id 0"
}

# List all valid NPU Phy-IDs (from /dev/davinci* or DCAT_NPU_CHIPS if set)
# DCAT_NPU_CHIPS="0,1,2,3" 可指定允许使用的芯片范围
npu_list_chips() {
    if [ -n "$DCAT_NPU_CHIPS" ]; then
        # 用户指定范围：支持逗号分隔或连字符范围
        echo "$DCAT_NPU_CHIPS" | tr ',' '\n' | while read -r c; do
            case "$c" in
                *-*)
                    start=${c%%-*}; end=${c##*-}
                    case "$start" in *[!0-9]*|"") continue;; esac
                    case "$end"   in *[!0-9]*|"") continue;; esac
                    n=$start; while [ "$n" -le "$end" ]; do
                        $HCCN_TO hccn_tool -i "$n" -link -g 2>/dev/null | grep -q 'link' && echo "$n"
                        n=$((n + 1))
                    done
                    ;;
                *)
                    case "$c" in *[!0-9]*|"") continue;; esac
                    $HCCN_TO hccn_tool -i "$c" -link -g 2>/dev/null | grep -q 'link' && echo "$c"
                    ;;
            esac
        done
    else
        # 从 /dev/davinci* 设备文件动态获取 chip 列表（不硬编码数量）
        for d in /dev/davinci[0-9]*; do
            [ -e "$d" ] || continue
            basename "$d" | grep -oE '[0-9]+$'
        done | sort -n
    fi
}

# Execute callback for each chip: chip has value → once; chip empty → all devices
# Exit code: 0 if any chip confirms fault, 1 if none (for dispatch confirmed flag)
# Usage: npu_foreach_chip 'command with $HCCN'
npu_foreach_chip() {
    if [ -n "$chip" ]; then
        eval "$1"
    else
        _rc=1
        for c in $(npu_list_chips); do
            echo "=== chip $c ==="
            _oc=$chip; _oh=$HCCN
            chip=$c; HCCN="hccn"
            eval "$1" && _rc=0
            chip=$_oc; HCCN=$_oh
        done
        return $_rc
    fi
}

# Kill a stress process by PID, verifying it's actually our _npu_stress
# (prevents PID reuse killing unrelated processes)
npu_kill_stress() {
    pid="$1"
    [ -z "$pid" ] && return 0
    if [ -f "/proc/$pid/cmdline" ] && grep -qa '_npu_stress' "/proc/$pid/cmdline" 2>/dev/null; then
        kill -9 "$pid" 2>/dev/null
        wait "$pid" 2>/dev/null
    else
        # PID doesn't exist or isn't ours — safe to ignore
        :
    fi
}

# sidecar_save <uid> <chip> <value>
sidecar_save() {
    printf '%s\n' "$3" > "/tmp/dcat-$1-$2.bak"
}
# sidecar_load <uid> <chip>  (echoes value, empty if missing)
sidecar_load() {
    cat "/tmp/dcat-$1-$2.bak" 2>/dev/null
}
# sidecar_clear <uid> <chip>
sidecar_clear() {
    rm -f "/tmp/dcat-$1-$2.bak" 2>/dev/null
}

# 条目型 NPU 故障 query 无参（dcat query <uid>）：按 sidecar 记录的 spec 判定。
# 若无 sidecar（从未注入或已 clean）→ 该 chip 非活跃。修复旧实现 `grep -F ""`
# 恒匹配 → clean 后误报 FAULT CONFIRMED 的系统性 bug。
npu_sidecar_load() {
    _uid="$1"; _bak="$2"
    ip=''; mac=''; dev=''; addr=''; mask=''; table=''; dir=''
    case "$_uid" in
        rNPU_arp)     dev=$(awk -F= '/^dev=/{print $2}' "$_bak" 2>/dev/null); ip=$(awk -F= '/^ip=/{print $2}' "$_bak" 2>/dev/null) ;;
        rNPU_route)   addr=$(awk -F= '/^addr=/{print $2}' "$_bak" 2>/dev/null); mask=$(awk -F= '/^mask=/{print $2}' "$_bak" 2>/dev/null) ;;
        rNPU_iproute) ip=$(awk -F= '/^ip=/{print $2}' "$_bak" 2>/dev/null); table=$(awk -F= '/^table=/{print $2}' "$_bak" 2>/dev/null) ;;
        rNPU_iprule)  dir=$(awk -F= '/^dir=/{print $2}' "$_bak" 2>/dev/null); ip=$(awk -F= '/^ip=/{print $2}' "$_bak" 2>/dev/null) ;;
    esac
}

# npu_query_noargs <uid> '<presence_check using $HCCN/$ip/$table/...>'
npu_query_noargs() {
    _uid="$1"; _check="$2"
    _any=0
    for c in $(npu_list_chips); do
        _bak="/tmp/dcat-$_uid-$c.bak"
        [ -f "$_bak" ] || continue
        chip=$c; HCCN="hccn"
        npu_sidecar_load "$_uid" "$_bak"
        eval "$_check"
        if [ $? = 0 ]; then
            echo "=== chip $c ==="
            echo "FAULT CONFIRMED"
            _any=1
        fi
    done
    if [ "$_any" = 0 ]; then
        echo "FAULT NOT ACTIVE"
        return 1
    fi
    return 0
}

# 条目型 del 重试：hccn_tool 在并发/瞬时 busy 时 del 偶发失败, 但实际条目已删。
# 以"del 成功 或 回读确认条目不存在"为成功（fault_present clean 语义：!grep ip）。
# Usage: npu_del_retry <uid> <-subcmd> [params...]（如：npu_del_retry rNPU_iprule -ip_rule dir from ip x）
npu_del_retry() {
    _uid="$1"; shift
    _ok=1
    _n=0
    _sleep=1
    while [ "$_n" -lt 5 ]; do
        _n=$((_n + 1))
        if $HCCN "$@" >/dev/null 2>&1; then _ok=0; break; fi
        sleep "$_sleep"
        # 指数退避应对 hccn_tool 瞬时 busy（外部监控进程并发争用）
        _sleep=$((_sleep * 2))
    done
    if [ "$_ok" != 0 ]; then
        echo "hccn_tool del ($_uid) failure after 5 tries (may already be deleted)" >&2
    fi
    # 即使 del 返回失败, 交回调用方的 fault_present 以实际状态判成败
    return 0
}

# 条目型 clean 组合重试：del + fault_present 读回，两者在共享节点的
# busy 风暴中偶发"del 命令返回 OK 但条目仍在/或读回瞬时查不到"。
# 以 fault_present(clean 语义=条目已不在) 为唯一判据，失败则再 del 一轮。
# Usage: npu_clean_verify <uid> <fault_present-cmd> <del 参数...>
#   <fault_present-cmd> 用 <模块fault_present> （在 clean 上下文中由调用方 eval）。
npu_clean_verify() {
    _uid="$1"; _chk="$2"; shift 2
    _n=0
    _sleep=1
    while [ "$_n" -lt 6 ]; do
        _n=$((_n + 1))
        # 先查是否已清除（前序 del 可能已生效）
        if eval "$_chk"; then return 0; fi
        $HCCN "$@" >/dev/null 2>&1
        sleep "$_sleep"
        _sleep=$((_sleep * 2))
    done
    # 最后一轮仍失败才判失败（交回调用方 fault_present 给出错误详情）
    eval "$_chk"
}
