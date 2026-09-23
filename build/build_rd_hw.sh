#!/bin/bash
# RustDesk 1.4.9 armv7 + mediacodec 硬解构建脚本（复刻官方 CI flutter-build.yml 的 build-rustdesk-android job）
set -euo pipefail

export ANDROID_NDK_HOME="$HOME/Library/Android/sdk/ndk/28.2.13676358"   # r28c，与 CI NDK_VERSION 一致
export ANDROID_NDK_ROOT="$ANDROID_NDK_HOME"
export VCPKG_ROOT="$HOME/rd_mc/vcpkg"
export CARGO_INCREMENTAL=0
export NDK_TOOLCHAIN="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/darwin-x86_64"
export BINDGEN_EXTRA_CLANG_ARGS="--sysroot=$NDK_TOOLCHAIN/sysroot --target=armv7a-linux-androideabi21"
export SODIUM_LIB_DIR="$VCPKG_ROOT/installed/arm-android/lib"
export PKG_CONFIG_PATH="/opt/homebrew/lib/pkgconfig"

cd "$HOME/rd_mc/rustdesk"

# 1) vcpkg 依赖（manifest 模式，arm-neon-android 三元组；构建脚本期望目录名 arm-android）
if [ ! -d "$VCPKG_ROOT/installed/arm-android" ]; then
  "$VCPKG_ROOT/vcpkg" install --triplet arm-neon-android --x-install-root="$VCPKG_ROOT/installed"
  mv "$VCPKG_ROOT/installed/arm-neon-android" "$VCPKG_ROOT/installed/arm-android"
fi

# 2) 编译（与官方唯一差异 = 多了 mediacodec 特性）
cargo ndk --platform 21 --target armv7-linux-androideabi build --locked --release --lib --features flutter,hwcodec,mediacodec

ls -la target/armv7-linux-androideabi/release/liblibrustdesk.so
