#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Run one Project-X build with root-level logging.

Usage:
  scripts/run-build-with-log.sh <target> <variant> [device]

Examples:
  scripts/run-build-with-log.sh hw chisel_core
  scripts/run-build-with-log.sh sw_emu hybrid xilinx_u55c_gen3x16_xdma_3_202210_1
USAGE
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

TARGET="${1:-}"
VARIANT="${2:-}"
DEVICE="${3:-xilinx_u55c_gen3x16_xdma_3_202210_1}"

if [[ -z "$TARGET" || -z "$VARIANT" ]]; then
    usage >&2
    exit 2
fi

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_DIR="$PROJECT_ROOT/logs"
mkdir -p "$LOG_DIR"

timestamp() {
    date '+%Y-%m-%d %H:%M:%S'
}

prepend_block() {
    local file="$1"
    local block="$2"
    local tmp
    tmp="$(mktemp)"
    {
        printf '%s' "$block"
        if [[ -f "$file" ]]; then
            cat "$file"
        fi
    } > "$tmp"
    mv "$tmp" "$file"
}

SESSION_LOG="$LOG_DIR/build_${VARIANT}_${TARGET}_$(date +%Y%m%d_%H%M%S).log"
HISTORY_LOG="$LOG_DIR/build_history.log"
START_TIME="$(timestamp)"
STATUS="FAIL"

echo "==== build TARGET=$TARGET VARIANT=$VARIANT ===="
echo "开始时间: $START_TIME"

cd "$PROJECT_ROOT"
if make build TARGET="$TARGET" VARIANT="$VARIANT" DEVICE="$DEVICE" 2>&1 | tee "$SESSION_LOG"; then
    STATUS="PASS"
fi

END_TIME="$(timestamp)"
ENTRY="$(cat <<EOF
[${END_TIME}] result=${STATUS} target=${TARGET} variant=${VARIANT}
start=${START_TIME}
end=${END_TIME}
log=${SESSION_LOG}

EOF
)"
prepend_block "$HISTORY_LOG" "$ENTRY"

echo "结束时间: $END_TIME"
echo "结果: $STATUS"
echo "日志: $SESSION_LOG"
echo "历史: $HISTORY_LOG"

if [[ "$STATUS" != "PASS" ]]; then
    exit 1
fi
