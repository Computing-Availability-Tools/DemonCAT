#!/bin/sh
# tests/check_syntax.sh — Tier 2: syntax check all scripts
for f in src/scripts/*/*.sh src/scripts/npu/_common.sh; do
    [ -f "$f" ] || continue
    sh -n "$f" || { echo "SYNTAX ERROR in $f"; exit 1; }
done
echo "All scripts pass syntax check"
