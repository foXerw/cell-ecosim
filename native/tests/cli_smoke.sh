#!/usr/bin/env bash
# Smoke test for the headless CLI: exit codes, output shape, determinism.
set -u
BIN="${1:-build/native/ecosim-cli.exe}"

fail() { echo "cli_smoke FAIL: $1" >&2; exit 1; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

"$BIN" bench --w 128 --h 128 --gens 50 --seed 7 --threads 4 | grep -q 'gens_per_sec=' \
  || fail "bench 输出缺少 gens_per_sec"

"$BIN" run --w 64 --h 64 --gens 3 --seed 7 --density 250 --out "$TMP/out.rle" \
  || fail "run 返回非 0"
[ -s "$TMP/out.rle" ] || fail "run 没有写出 rle"

"$BIN" bench --w 0 --h 8 --gens 1 >/dev/null 2>&1
[ $? -eq 2 ] || fail "参数错误应返回 2"

"$BIN" hash --rle nope-does-not-exist.rle >/dev/null 2>&1
[ $? -eq 3 ] || fail "引擎失败应返回 3"

A="$("$BIN" hash --rle "$TMP/out.rle" --w 64 --h 64 --gens 10)"
B="$("$BIN" hash --rle "$TMP/out.rle" --w 64 --h 64 --gens 10)"
[ "$A" = "$B" ] || fail "同种子两次运行哈希不一致：$A vs $B"

echo "cli_smoke ok ($A)"
