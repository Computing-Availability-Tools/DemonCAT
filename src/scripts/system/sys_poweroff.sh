#!/bin/sh
# rSYS_poweroff: power off or reboot the machine (inject-only, irreversible).
# mode=0: 下电后重启 (reboot); mode=1: 下电后不重启 (poweroff, stay off).
case "${DCAT_OP:-inject}" in
    inject)
        mode=${DCAT_PARAM_MODE:?missing required param: mode}
        case "$mode" in
            0)
                echo "injected reboot (down + restart) mode=0"
                reboot 2>/dev/null || shutdown -r now 2>/dev/null || halt --reboot 2>/dev/null
                echo "reboot failed (need root?)" >&2; exit 1
                ;;
            1)
                echo "injected poweroff (down, stay off) mode=1"
                poweroff 2>/dev/null || shutdown -h now 2>/dev/null || halt -p 2>/dev/null
                echo "poweroff failed (need root?)" >&2; exit 1
                ;;
            *)
                echo "mode must be 0 (reboot) or 1 (poweroff), got: '$mode'" >&2; exit 1
                ;;
        esac
        ;;
    clean)
        echo "rSYS_poweroff is inject-only; nothing to clean (machine rebooted or off)" >&2; exit 0
        ;;
    query)
        echo "rSYS_poweroff is inject-only" >&2; exit 1
        ;;
    *) echo "unknown op: $DCAT_OP" >&2; exit 1;;
esac
