#!/bin/sh
# install.sh — 把本目录 skills/ 下的技能装到各 agent 运行时的 skill 目录。
#
# 用法:
#   sh .agents/install.sh                 # 装到所有已知运行时目录（软链，随仓库更新）
#   sh .agents/install.sh --target DIR    # 只装到指定目录
#   sh .agents/install.sh --copy          # 用拷贝而非软链
#
# 已知运行时目录: opencode(~/.config/opencode/skill)、Claude(~/.claude/skills)、
#               通用别名(~/.agents/skills)、项目(./.opencode/skill)
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
COPY=0
TARGET=""
while [ $# -gt 0 ]; do
  case "$1" in
    --copy) COPY=1 ;;
    --link) COPY=0 ;;
    --target) TARGET=${2:-}; shift ;;
    -h|--help) echo "usage: sh .agents/install.sh [--target DIR] [--copy]"; exit 0 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
  shift
done

if [ -n "$TARGET" ]; then
  DIRS="$TARGET"
else
  DIRS="$HOME/.config/opencode/skill
$HOME/.claude/skills
$HOME/.agents/skills
$PWD/.opencode/skill"
fi

install_skill() { # $1=skill src dir  $2=dest root
  _name=$(basename "$1")
  _dest="$2/$_name"
  if mkdir -p "$2" 2>/dev/null && rm -rf "$_dest" 2>/dev/null; then
    if [ "$COPY" = 1 ]; then cp -R "$1" "$_dest"; else ln -s "$1" "$_dest"; fi
    # 保证脚本可执行（拷贝到 ext4 可能丢 +x）
    if [ -d "$_dest/scripts" ]; then chmod +x "$_dest"/scripts/* 2>/dev/null || true; fi
    echo "  ok   $_dest"
  else
    echo "  skip $2 (not writable)"
  fi
}

printf '%s\n' "$DIRS" | while IFS= read -r d; do
  [ -n "$d" ] || continue
  echo "== $d =="
  for s in "$HERE"/skills/*/; do
    [ -d "$s" ] || continue
    install_skill "$s" "$d"
  done
done
echo "done."
