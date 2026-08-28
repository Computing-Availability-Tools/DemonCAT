#!/bin/sh
# rNPU_hbm_load: HBM stress via aclrtMalloc+memset.
# inject: run _npu_stress hbm in background, write pidfile
# clean:  kill stress process
# query:  npu-smi info -t usages (check HBM Usage Rate)
. "$(dirname "$0")/_common.sh"
chip=${DCAT_PARAM_CHIP:-}
if [ -n "$chip" ]; then npu_validate_chip "$chip" || { echo "chip validation failed" >&2; exit 1; }; fi
SIDECAR="/tmp/dcat-rNPU_hbm_load-$chip.pid"
STRESS_BIN="$(cd "$(dirname "$0")/../../.." && pwd)/build/_npu_stress"

# 从 `npu-smi info` 主表解析指定 NPU 卡 HBM-Usage(MB) 列的精确值 "used total"
npu_hbm_usage() {
    npu-smi info 2>/dev/null | awk -F'|' -v want="$1" '
        function trim(s){gsub(/^ +| +$/, "", s); return s}
        /^\| / && $0 !~ /0000:/ {
            c=trim($2)
            if (c ~ /^[0-9]+[ \t]+/) {
                nid=c; sub(/[ \t].*/,"",nid)
                getline_data = (nid==want)?1:0
            } else { getline_data=0 }
        }
        /0000:/ && getline_data {
            vals=$4
            if (match(vals, /[0-9]+[ \t]*\/[ \t]*[0-9]+[ \t]*$/, m)) {
                s=m[0]; gsub(/[ \t]/, "", s); split(s,t,/\//); print t[1], t[2]
            }
            getline_data=0
        }'
}

# parse size string to MB: 2G=2048, 500M=500, 500=500
size_to_mb() {
    s=$1
    case "$s" in
        *[0-9]G|*[0-9]g) num=${s%[Gg]}; echo $((num * 1024));;
        *[0-9]M|*[0-9]m) num=${s%[Mm]}; echo "$num";;
        *[0-9]) echo "$s";;
        *) return 1;;
    esac
}

case "${DCAT_OP:-inject}" in
    inject)
        : ${chip:?missing required param: chip}
        # Kill existing stress on same chip (prevent orphan)
        if [ -f "$SIDECAR" ]; then
            for _old in $(cat "$SIDECAR" 2>/dev/null); do npu_kill_stress "$_old"; done
            rm -f "$SIDECAR"
        fi
        npu_check_env
        if [ ! -x "$STRESS_BIN" ]; then
            echo "ERROR: _npu_stress not built. Run: cd build && cmake .. && make _npu_stress" >&2; exit 1
        fi
        dev_id=$(npu_acl_dev_id "$chip")
        [ -z "$dev_id" ] && { npu_acl_dev_id_err "$chip"; exit 1; }
        size_raw=${DCAT_PARAM_SIZE:?missing required param: size}
        size_mb=$(size_to_mb "$size_raw") || { echo "invalid size: $size_raw (use 500M, 2G, 500)" >&2; exit 1; }
        "$STRESS_BIN" hbm "$dev_id" "$size_mb" 0 0 >/dev/null 2>&1 &
        pid=$!
        echo "$pid" > "$SIDECAR"
        sleep 2
        if ! kill -0 "$pid" 2>/dev/null; then
            rm -f "$SIDECAR"
            echo "HBM stress failed: cannot allocate ${size_mb}MB on chip $chip (HBM insufficient?)" >&2
            exit 1
        fi
        proc_mem=$(npu-smi info 2>/dev/null | grep '_npu_stress' | awk -F'|' '{gsub(/^ +| +$/,"",$5); print $5}')
        if [ -n "$proc_mem" ] && [ "$proc_mem" -lt $((size_mb / 2)) ] 2>/dev/null; then
            npu_kill_stress "$pid"
            rm -f "$SIDECAR"
            echo "HBM stress failed: only ${proc_mem}MB allocated (requested ${size_mb}MB), HBM insufficient" >&2
            exit 1
        fi
        echo "HBM stress started on chip $chip (dev $dev_id, pid $pid, ${size_mb}MB)"
        ;;
    clean)
        # stateless: chip 为空时遍历所有 sidecar（与 query 的 glob 一致，
        # 避免 `dcat clean rNPU_hbm_load` 假成功空操作留下孤儿进程）
        if [ -z "$chip" ]; then
            cleaned=0
            for f in /tmp/dcat-rNPU_hbm_load-*.pid; do
                [ -f "$f" ] || continue
                c=$(echo "$f" | sed 's/.*-//;s/\.pid//')
                for _p in $(cat "$f" 2>/dev/null); do npu_kill_stress "$_p"; done
                rm -f "$f"
                echo "HBM stress stopped on chip $c"
                cleaned=1
            done
            [ "$cleaned" = 1 ] || echo "no active HBM stress"
            exit 0
        fi
        if [ -f "$SIDECAR" ]; then
            for _p in $(cat "$SIDECAR" 2>/dev/null); do npu_kill_stress "$_p"; done
            rm -f "$SIDECAR"
            echo "HBM stress stopped on chip $chip"
        else
            echo "no active HBM stress on chip $chip"
        fi
        ;;
    query)
        if [ -z "$chip" ]; then
            found=0
            for f in /tmp/dcat-rNPU_hbm_load-*.pid; do
                [ -f "$f" ] || continue
                c=$(echo "$f" | sed 's/.*-//;s/\.pid//')
                pid=$(cat "$f" 2>/dev/null)
                kill -0 "$pid" 2>/dev/null || { rm -f "$f"; continue; }
                echo "FAULT CONFIRMED: HBM stress active on chip $c (pid $pid)"
                card_chip=$(npu_phy_to_card "$c"); card_id=${card_chip%% *}; chip_id=${card_chip##* }
                hbm_pct=$(npu-smi info -t usages -i "$card_id" -c "$chip_id" 2>/dev/null | awk '/HBM Usage Rate/{print $NF}')
                set -- $(npu_hbm_usage "$card_id")
                hbm_used=${1:-0} hbm_total=${2:-?}
                echo "  HBM Usage: ${hbm_used}MB / ${hbm_total}MB (${hbm_pct:-?}%)"
                found=1
            done
            [ "$found" = 1 ] && exit 0 || { echo "FAULT NOT ACTIVE: no HBM stress"; exit 1; }
        elif [ -f "$SIDECAR" ] && kill -0 "$(cat "$SIDECAR")" 2>/dev/null; then
            echo "FAULT CONFIRMED: HBM stress active (pid $(cat $SIDECAR))"
            card_chip=$(npu_phy_to_card "$chip"); card_id=${card_chip%% *}; chip_id=${card_chip##* }
            hbm_pct=$(npu-smi info -t usages -i "$card_id" -c "$chip_id" 2>/dev/null | awk '/HBM Usage Rate/{print $NF}')
            set -- $(npu_hbm_usage "$card_id")
            hbm_used=${1:-0} hbm_total=${2:-?}
            echo "HBM Usage: ${hbm_used}MB / ${hbm_total}MB (${hbm_pct:-?}%)"
            exit 0
        else
            rm -f "$SIDECAR" 2>/dev/null
            echo "FAULT NOT ACTIVE: no HBM stress"
            exit 1
        fi
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1 ;;
esac
