#!/usr/bin/env bash
# Sync libbbpjiuwen.so / libmcp.so from bbpjiuwen-linux into this repo.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${JIUWEN_ROOT:-$(cd "$ROOT/../bbpjiuwen-linux" && pwd)}"
DST="$ROOT/third_party/bbpjiuwen/lib"
mkdir -p "$DST"
cp -f "$SRC/lib/libbbpjiuwen.so" "$DST/"
cp -f "$SRC/lib/libmcp.so" "$DST/"
ls -lh "$DST"
echo "Synced from $SRC/lib -> $DST"
