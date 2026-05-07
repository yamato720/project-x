#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Build Project-X variants in sequence.

Usage:
  scripts/build-all-variants.sh [sw|hw|both]

Examples:
  scripts/build-all-variants.sh sw
  scripts/build-all-variants.sh hw
  scripts/build-all-variants.sh both
USAGE
}

MODE="${1:-both}"

case "$MODE" in
    sw|hw|both) ;;
    -h|--help)
        usage
        exit 0
        ;;
    *)
        echo "ERROR: unknown mode: $MODE" >&2
        usage >&2
        exit 2
        ;;
esac

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

variants=(hls hybrid chisel_core)
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

run_make_with_summary() {
    local target="$1"
    local variant="$2"
    local history_log="$LOG_DIR/build_history.log"
    local start_time
    local end_time
    local status
    local session_log

    start_time="$(timestamp)"
    echo "==== build TARGET=$target VARIANT=$variant ===="
    echo "开始时间: $start_time"

    if bash scripts/run-build-with-log.sh "$target" "$variant"; then
        status="PASS"
    else
        status="FAIL"
    fi

    end_time="$(timestamp)"
    session_log="$(awk '/^log=/{print substr($0,5); exit}' "$history_log" 2>/dev/null || true)"
    prepend_block "$SUMMARY_LOG" "$(cat <<EOF
variant=$variant
target=$target
start=$start_time
end=$end_time
result=$status
log=$session_log

EOF
)"

    echo "结束时间: $end_time"
    echo "结果: $status"

    if [[ "$status" != "PASS" ]]; then
        return 1
    fi
}

run_builds() {
    local target="$1"
    for variant in "${variants[@]}"; do
        run_make_with_summary "$target" "$variant"
    done
}

SUMMARY_LOG="$LOG_DIR/build_all_variants_${MODE}_$(date +%Y%m%d_%H%M%S).log"
prepend_block "$SUMMARY_LOG" "$(cat <<EOF
Project-X build summary
mode=$MODE
summary_start=$(timestamp)

EOF
)"

if [[ "$MODE" == "sw" || "$MODE" == "both" ]]; then
    run_builds sw_emu
fi

if [[ "$MODE" == "hw" || "$MODE" == "both" ]]; then
    run_builds hw
fi

prepend_block "$SUMMARY_LOG" "$(cat <<EOF
summary_end=$(timestamp)

EOF
)"

echo "总日志: $SUMMARY_LOG"
