#!/bin/sh
# rSYS_panic: trigger kernel panic via sysrq 'c' (inject-only, irreversible).
case "${DCAT_OP:-inject}" in
    inject)
        if [ ! -w /proc/sysrq-trigger ]; then
            echo "sysrq not available; enable: echo 1 > /proc/sys/kernel/sysrq" >&2; exit 1
        fi
        echo "injected kernel panic via sysrq 'c' (system will crash)"
        echo c > /proc/sysrq-trigger 2>/dev/null
        echo "WARNING: sysrq 'c' did not crash — kernel may have sysrq masked" >&2; exit 1
        ;;
    clean)
        echo "rSYS_panic is inject-only; nothing to clean (system rebooted)" >&2; exit 0
        ;;
    query)
        echo "rSYS_panic is inject-only" >&2; exit 1
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
