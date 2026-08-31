#!/usr/bin/env bash
# markdown-tool 建置腳本
#
# 固定使用 g++-12：Ubuntu 22.04 的 libstdc++6 執行期是 12.x（PPA），
# Qt 6.2.4 是對著它建的，用預設的 g++-11 連結會缺 GLIBCXX_3.4.30。
set -euo pipefail
cd "$(dirname "$0")"

BUILD_DIR="${BUILD_DIR:-build}"
BUILD_TYPE="${BUILD_TYPE:-Release}"

cmake -S . -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_C_COMPILER=gcc-12 \
    -DCMAKE_CXX_COMPILER=g++-12

cmake --build "$BUILD_DIR" -j"$(nproc)"
ctest --test-dir "$BUILD_DIR" --output-on-failure
