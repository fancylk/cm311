#!/bin/bash
# csd-fix build: Rust .so (build_rd_hw.sh) -> jniLibs -> flutter apk
set -uo pipefail
export ANDROID_NDK_HOME="$HOME/Library/Android/sdk/ndk/28.2.13676358"
export ANDROID_NDK_ROOT="$ANDROID_NDK_HOME"
export VCPKG_ROOT="$HOME/rd_mc/vcpkg"
export CARGO_INCREMENTAL=0
export NDK_TOOLCHAIN="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/darwin-x86_64"
export BINDGEN_EXTRA_CLANG_ARGS="--sysroot=$NDK_TOOLCHAIN/sysroot --target=armv7a-linux-androideabi21"
export SODIUM_LIB_DIR="$VCPKG_ROOT/installed/arm-android/lib"
export PKG_CONFIG_PATH="/opt/homebrew/lib/pkgconfig"
export PUB_HOSTED_URL=https://pub.flutter-io.cn
export FLUTTER_STORAGE_BASE_URL=https://storage.flutter-io.cn
export JAVA_HOME=/opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home
export PATH="$JAVA_HOME/bin:$PATH"
MC=$HOME/rd_mc

echo "[build] step1: rust so"
bash "$MC/build_rd_hw.sh" || { echo "[build] FATAL rust"; exit 1; }

echo "[build] step2: copy so into jniLibs"
cp "$MC/rustdesk/target/armv7-linux-androideabi/release/liblibrustdesk.so" \
   "$MC/rustdesk/flutter/android/app/src/main/jniLibs/armeabi-v7a/librustdesk.so" || { echo "[build] FATAL cp"; exit 1; }
ls -la "$MC/rustdesk/flutter/android/app/src/main/jniLibs/armeabi-v7a/"

echo "[build] step3: flutter apk"
cd "$MC/rustdesk/flutter"
"$MC/f3245/flutter/bin/flutter" build apk --release --target-platform android-arm || { echo "[build] FATAL flutter"; exit 1; }
ls -la build/app/outputs/flutter-apk/app-release.apk
md5 build/app/outputs/flutter-apk/app-release.apk
echo "[build] APK_BUILD_OK"
