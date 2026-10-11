#!/bin/sh
# Build the Kindle remote UF2 from a local ZMK workspace.
# Usage: ./build.sh [extra west build args]   (e.g. -S zmk-usb-logging)
set -e
here=$(cd "$(dirname "$0")" && pwd)
ws="$here/../zmk-workspace"
. "$ws/.venv/bin/activate"
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr ZEPHYR_SDK_INSTALL_DIR="$ws/zephyr-sdk-0.17.0"
cd "$ws/zmk"
git apply --check "$here/patches/0001-kindle-remote-advertising.patch" 2>/dev/null \
  && git apply "$here/patches/0001-kindle-remote-advertising.patch"
west build -p auto -s app -d "$ws/build" -b xiao_ble//zmk "$@" -- -DZMK_CONFIG="$here/config"
cp "$ws/build/zephyr/zmk.uf2" "$here/kindle_remote.uf2"
echo "Built $here/kindle_remote.uf2"
