#!/bin/bash
# 部署/恢复 Mac RustDesk 降采样补丁（1080p 流方案）
# 用法:
#   deploy_patched_mac.sh patch    # 换入补丁 dylib（需先做一次屏幕录制授权）
#   deploy_patched_mac.sh restore  # 恢复官方 dylib
set -euo pipefail
APP=/Applications/RustDesk.app
DYLIB="$APP/Contents/Frameworks/liblibrustdesk.dylib"
PATCHED=$HOME/rd_mc/rustdesk/target/aarch64-apple-darwin/release/liblibrustdesk.dylib
OFFICIAL=$HOME/rd_mc/liblibrustdesk_official_backup.dylib

case "${1:-}" in
  patch)
    cp "$PATCHED" /tmp/liblibrustdesk.dylib
    # App Management 保护：删旧（允许）→ 装新（允许新建）
    rm -f "$DYLIB"
    cp /tmp/liblibrustdesk.dylib "$DYLIB"
    codesign --force --sign - --options runtime "$DYLIB"
    codesign --force --deep --sign - "$APP"
    xattr -rc "$APP" 2>/dev/null || true
    echo "补丁已换入。首次运行需授权：系统设置→隐私与安全性→屏幕录制→RustDesk"
    ;;
  restore)
    rm -f "$DYLIB"
    cp "$OFFICIAL" "$DYLIB"
    codesign --force --sign - --options runtime "$DYLIB"
    codesign --force --deep --sign - "$APP"
    xattr -rc "$APP" 2>/dev/null || true
    echo "已恢复官方 dylib（原 TCC 授权对应的签名无法复原，首次仍可能需重新授权屏幕录制一次）"
    ;;
  *) echo "usage: $0 patch|restore"; exit 1;;
esac
# 重启服务与进程
launchctl bootout gui/$(id -u)/com.carriez.RustDesk_server 2>/dev/null || true
pkill -9 -f "RustDesk" 2>/dev/null || true
sleep 2
rm -f /tmp/RustDesk-501/ipc* 2>/dev/null || true
open "$APP"
sleep 6
nohup "$APP/Contents/MacOS/RustDesk" --server > /tmp/rd_server.log 2>&1 &
sleep 5
echo "进程:"; pgrep -fl "MacOS/RustDesk" || true
