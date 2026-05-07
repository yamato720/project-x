#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Run Project-X variant tests in sequence.

Usage:
  scripts/test-all-variants.sh [sw|hw|both] [rows] [scale] [x0]

Examples:
  scripts/test-all-variants.sh sw
  scripts/test-all-variants.sh sw 8 2 1
  scripts/test-all-variants.sh hw 8 2 1
  scripts/test-all-variants.sh both 8 2 1
USAGE
}

MODE="${1:-sw}"
ROWS="${2:-8}"
SCALE="${3:-2}"
X0="${4:-1}"

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
SUMMARY_FILE="$(mktemp)"
trap 'rm -f "$SUMMARY_FILE"' EXIT

extract_metric() {
    local key="$1"
    local file="$2"
    awk -F= -v pattern="  ${key}" '$1 == pattern {print $2; exit}' "$file"
}

print_summary() {
    if [[ ! -s "$SUMMARY_FILE" ]]; then
        return
    fi

    echo
    echo "==== Timing Summary ===="
    printf "%-14s %-12s %-12s %-12s %-12s %-12s\n" \
        "variant" "kernel_min" "kernel_avg" "kernel_max" "buffer_h2d" "total"

    while IFS='|' read -r variant kernel_min kernel_avg kernel_max buffer_h2d total; do
        printf "%-14s %-12s %-12s %-12s %-12s %-12s\n" \
            "$variant" "$kernel_min" "$kernel_avg" "$kernel_max" "$buffer_h2d" "$total"
    done < "$SUMMARY_FILE"
}

run_tests() {
    local target="$1"
    local make_target
    local log_dir
    local log_file
    local kernel_min
    local kernel_avg
    local kernel_max
    local buffer_h2d
    local total

    if [[ "$target" == "sw" ]]; then
        make_target="run-sw-existing"
    else
        make_target="run-hw-existing"
    fi

    for variant in "${variants[@]}"; do
        echo "==== $make_target VARIANT=$variant ROWS=$ROWS SCALE=$SCALE X0=$X0 ===="
        log_dir="$PROJECT_ROOT/logs/test_runs"
        mkdir -p "$log_dir"
        log_file="$log_dir/${make_target}_${variant}_$(date +%Y%m%d_%H%M%S).log"
        if ! make "$make_target" VARIANT="$variant" ROWS="$ROWS" SCALE="$SCALE" X0="$X0" 2>&1 | tee "$log_file"; then
            return 1
        fi

        kernel_min="$(extract_metric kernel_min "$log_file")"
        kernel_avg="$(extract_metric kernel_avg "$log_file")"
        kernel_max="$(extract_metric kernel_max "$log_file")"
        buffer_h2d="$(extract_metric buffer_h2d "$log_file")"
        total="$(extract_metric total "$log_file")"

        printf '%s|%s|%s|%s|%s|%s\n' \
            "$variant" "${kernel_min:--}" "${kernel_avg:--}" "${kernel_max:--}" "${buffer_h2d:--}" "${total:--}" \
            >> "$SUMMARY_FILE"
    done
}

if [[ "$MODE" == "sw" || "$MODE" == "both" ]]; then
    run_tests sw
fi

if [[ "$MODE" == "hw" || "$MODE" == "both" ]]; then
    run_tests hw
fi

print_summary
