#!/bin/sh
# config/scripts/network/net_loss.sh
case "$DCAT_OP" in
    inject) echo "injected loss iface=$DCAT_PARAM_IFACE pct=$DCAT_PARAM_LOSS_PCT"; exit 0 ;;
    clean)  echo "cleaned loss iface=$DCAT_PARAM_IFACE pct=$DCAT_PARAM_LOSS_PCT"; exit 0 ;;
    query)  echo "net query stub"; exit 0 ;;
    *)      echo "unknown op: $DCAT_OP" >&2; exit 1 ;;
esac
