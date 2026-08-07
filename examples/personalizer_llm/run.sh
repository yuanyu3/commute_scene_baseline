#!/usr/bin/env bash
# Build + run theta personalizer against real DeepSeek (WSL / Linux).
# Credentials are read by personalizer_llm from agent.env — do not source the file in bash
# (values may contain spaces, e.g. "Bearer sk-...").
#
# DEBUG:
#   PERSONALIZER_DEBUG=1 bash examples/personalizer_llm/run.sh
#   bash examples/personalizer_llm/run.sh --debug
#   bash examples/personalizer_llm/run.sh sa_service/etc/agent.env run_data --debug
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
EXAMPLE="$(cd "$(dirname "$0")" && pwd)"
BUILD="$EXAMPLE/build"
export JIUWEN_ROOT="${JIUWEN_ROOT:-$ROOT/../bbpjiuwen-linux}"

ENV_FILE="$ROOT/sa_service/etc/agent.env"
DATA="$EXAMPLE/run_data"
EXTRA_ARGS=()
POS=()
for a in "$@"; do
  case "$a" in
    --debug|-d) EXTRA_ARGS+=(--debug) ;;
    --no-fixture) EXTRA_ARGS+=(--no-fixture) ;;
    *) POS+=("$a") ;;
  esac
done
if [[ ${#POS[@]} -ge 1 ]]; then ENV_FILE="${POS[0]}"; fi
if [[ ${#POS[@]} -ge 2 ]]; then DATA="${POS[1]}"; fi
if [[ "${PERSONALIZER_DEBUG:-}" == "1" || "${SA_AGENT_DEBUG:-}" == "1" ]]; then
  EXTRA_ARGS+=(--debug)
fi
if [[ "${PERSONALIZER_NO_FIXTURE:-}" == "1" ]]; then
  EXTRA_ARGS+=(--no-fixture)
fi

if [[ ! -f "$ROOT/third_party/bbpjiuwen/lib/libbbpjiuwen.so" ]]; then
  echo "Missing synced so; running scripts/sync_jiuwen_libs.sh"
  bash "$ROOT/scripts/sync_jiuwen_libs.sh"
fi

if [[ ! -f "$ENV_FILE" ]]; then
  echo "Missing $ENV_FILE" >&2
  exit 1
fi

need() { command -v "$1" >/dev/null || { echo "need $1" >&2; exit 1; }; }
need cmake
if command -v clang++ >/dev/null; then
  CXX=clang++
else
  echo "clang++ not found" >&2
  exit 1
fi

cmake -S "$EXAMPLE" -B "$BUILD" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER="$CXX" \
  -DJIUWEN_ROOT="$JIUWEN_ROOT" \
  -DCMAKE_CXX_FLAGS="-stdlib=libc++" \
  -DCMAKE_EXE_LINKER_FLAGS="-stdlib=libc++"

cmake --build "$BUILD" -j"$(nproc 2>/dev/null || echo 4)"

export LD_LIBRARY_PATH="$ROOT/third_party/bbpjiuwen/lib:${JIUWEN_ROOT}/lib:${LD_LIBRARY_PATH:-}"
cd "$ROOT"
exec "$BUILD/personalizer_llm" "$ENV_FILE" "$DATA" "${EXTRA_ARGS[@]}"
