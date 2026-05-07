#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Package Vivado artifacts for all Project-X variants.

Usage:
  scripts/package-all-variants.sh [min|full|both]

Examples:
  scripts/package-all-variants.sh min
  scripts/package-all-variants.sh full
  scripts/package-all-variants.sh both
USAGE
}

MODE="${1:-both}"

case "$MODE" in
    min|full|both) ;;
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

run_packages() {
    local package_target="$1"
    for variant in "${variants[@]}"; do
        echo "==== $package_target VARIANT=$variant ===="
        make "$package_target" VARIANT="$variant"
    done
}

if [[ "$MODE" == "min" || "$MODE" == "both" ]]; then
    run_packages vivado-package
fi

if [[ "$MODE" == "full" || "$MODE" == "both" ]]; then
    run_packages vivado-package-full
fi
