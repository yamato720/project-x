#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Download Project-X Vivado viewing files from the build server.

Usage:
  scripts/download-vivado-view.sh USER@SERVER [DEST_DIR] [--min|--full|--reports-only]

Examples:
  scripts/download-vivado-view.sh USER@SERVER ./project-x-vivado --min
  scripts/download-vivado-view.sh USER@SERVER ./project-x-vivado --full

Notes:
  --min         routed DCP + reports + docs (default)
  --full        full Vitis/Vivado vpl directory + reports + docs
  --reports-only reports only, useful on macOS without Vivado
USAGE
}

REMOTE="${1:-}"
DEST_DIR="${2:-project-x-vivado-view}"
MODE="${3:---min}"

if [[ -z "$REMOTE" || "$REMOTE" == "-h" || "$REMOTE" == "--help" ]]; then
    usage
    exit 0
fi

case "$MODE" in
    --min|--full|--reports-only) ;;
    *)
        echo "ERROR: unknown mode: $MODE" >&2
        usage >&2
        exit 2
        ;;
esac

REMOTE_ROOT="${PROJECT_X_REMOTE_ROOT:-~/ProjectFS/Project-X}"
DEVICE="${PROJECT_X_DEVICE:-xilinx_u55c_gen3x16_xdma_3_202210_1}"
VARIANT="${PROJECT_X_VARIANT:-hybrid}"
LOCAL_DEST="$(mkdir -p "$DEST_DIR" && cd "$DEST_DIR" && pwd)"
REMOTE_BUILD_DIR="$REMOTE_ROOT/build/$VARIANT/hw/$DEVICE"
REMOTE_REPORT_DIR="$REMOTE_ROOT/reports/$VARIANT/hw/$DEVICE"

case "$(uname -s)" in
    Darwin)
        echo "INFO: macOS can download and inspect reports, but AMD Vivado 2022.2 is supported on Windows/Linux, not native macOS."
        ;;
esac

if command -v rsync >/dev/null 2>&1; then
    copy_dir() {
        rsync -avP "$1" "$2"
    }
else
    copy_dir() {
        scp -r "$1" "$2"
    }
fi

echo "Remote: $REMOTE"
echo "Remote root: $REMOTE_ROOT"
echo "Variant: $VARIANT"
echo "Destination: $LOCAL_DEST"
echo "Mode: $MODE"

if [[ "$MODE" == "--reports-only" ]]; then
    copy_dir "$REMOTE:$REMOTE_REPORT_DIR" "$LOCAL_DEST/reports-hw"
    echo "Downloaded reports to: $LOCAL_DEST/reports-hw"
    exit 0
fi

if [[ "$MODE" == "--min" ]]; then
    mkdir -p "$LOCAL_DEST/dcp"
    scp "$REMOTE:$REMOTE_BUILD_DIR/_x_temp/link/vivado/vpl/prj/prj.runs/impl_1/level0_wrapper_routed.dcp" "$LOCAL_DEST/dcp/"
    copy_dir "$REMOTE:$REMOTE_REPORT_DIR" "$LOCAL_DEST/reports-hw"
    copy_dir "$REMOTE:$REMOTE_ROOT/docs" "$LOCAL_DEST/docs"
    scp "$REMOTE:$REMOTE_ROOT/README.md" "$LOCAL_DEST/"
    echo "Downloaded minimal Vivado view package to: $LOCAL_DEST"
    echo "Open DCP with: vivado $LOCAL_DEST/dcp/level0_wrapper_routed.dcp"
    exit 0
fi

copy_dir "$REMOTE:$REMOTE_BUILD_DIR/_x_temp/link/vivado/vpl" "$LOCAL_DEST/vpl"
copy_dir "$REMOTE:$REMOTE_REPORT_DIR" "$LOCAL_DEST/reports-hw"
copy_dir "$REMOTE:$REMOTE_ROOT/docs" "$LOCAL_DEST/docs"
scp "$REMOTE:$REMOTE_ROOT/README.md" "$LOCAL_DEST/"
echo "Downloaded full Vivado project package to: $LOCAL_DEST"
echo "Open project with: vivado $LOCAL_DEST/vpl/prj/prj.xpr"
