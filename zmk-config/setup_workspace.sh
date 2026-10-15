#!/bin/sh
# Setup script to initialize a local ZMK workspace for building kindle_remote.uf2.
# Usage: ./setup_workspace.sh
set -e

here=$(cd "$(dirname "$0")" && pwd)
ws="$here/../zmk-workspace"
ZMK_REV=4dd95d78  # ZMK main revision the patch was made against (FINDINGS 6.2a)

echo "Creating ZMK workspace at: $ws"
mkdir -p "$ws"
cd "$ws"

# 1. Python virtual environment and west
if [ ! -d ".venv" ]; then
  echo "Setting up Python virtual environment..."
  python3 -m venv .venv
fi
. "$ws/.venv/bin/activate"
pip install --upgrade pip
pip install west

# 2. ZMK at the pinned revision; west topdir is zmk/ (build.sh runs from there)
if [ ! -d "zmk" ]; then
  echo "Cloning ZMK at $ZMK_REV..."
  git clone https://github.com/zmkfirmware/zmk zmk
  cd zmk
  git checkout "$ZMK_REV"
  west init -l app
  cd "$ws"
fi
# Idempotent; also repairs a recreated .venv (e.g. after moving the repo)
(cd zmk && west update && west zephyr-export && pip install -r zephyr/scripts/requirements.txt)

# 3. Download Zephyr SDK (v0.17.0 for ARM)
if [ ! -d "zephyr-sdk-0.17.0" ]; then
  SDK_TAR="zephyr-sdk-0.17.0_macos-aarch64.tar.xz"
  OS=$(uname -s | tr '[:upper:]' '[:lower:]')
  ARCH=$(uname -m)
  
  if [ "$OS" = "darwin" ]; then
    if [ "$ARCH" = "arm64" ]; then
      SDK_TAR="zephyr-sdk-0.17.0_macos-aarch64.tar.xz"
    else
      SDK_TAR="zephyr-sdk-0.17.0_macos-x86_64.tar.xz"
    fi
  elif [ "$OS" = "linux" ]; then
    if [ "$ARCH" = "aarch64" ]; then
      SDK_TAR="zephyr-sdk-0.17.0_linux-aarch64.tar.xz"
    else
      SDK_TAR="zephyr-sdk-0.17.0_linux-x86_64.tar.xz"
    fi
  fi

  echo "Downloading Zephyr SDK ($SDK_TAR)..."
  curl -L -O "https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v0.17.0/$SDK_TAR"
  tar -xf "$SDK_TAR"
  rm "$SDK_TAR"
  cd zephyr-sdk-0.17.0
  ./setup.sh -t arm-zephyr-eabi -h -c
  cd "$ws"
fi

echo ""
echo "=== ZMK Workspace Ready ==="
echo "Now run ./build.sh in zmk-config/ to build kindle_remote.uf2"
