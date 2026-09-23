# 官方版紧急恢复记录（2026-09-23 20:10，已验证成功）

补丁版 app（ad-hoc 重签）导致屏幕录制 TCC 失效 → 远程黑屏。恢复步骤（全程无需管理员密码）：

1. 下载官方包（gh-proxy）：
   curl -L -o /tmp/rd.dmg "https://gh-proxy.com/https://github.com/rustdesk/rustdesk/releases/download/1.4.9/rustdesk-1.4.9-aarch64.dmg"
2. hdiutil attach /tmp/rd.dmg -nobrowse
3. pkill -9 -f RustDesk
4. rm -rf /Applications/RustDesk.app && cp -R /Volumes/rustdesk-1.4.9/RustDesk.app /Applications/RustDesk.app
5. xattr -rc /Applications/RustDesk.app
6. open /Applications/RustDesk.app（root service 会自动拉起 --server）
7. 盒子重连 → VP9 稳定恢复（TCC 授权按官方签名自动重新生效，实测即时生效）

## 关键结论
- TCC（屏幕录制）授权按官方签名（TeamID HZF9JMC8YN）匹配，同包重装自动恢复，无需重新授权
- ad-hoc 重签的补丁版必须让用户手动重授屏幕录制（一次性）才能采集
- 补丁 dylib 备份：~/rd_mc/rustdesk/target/aarch64-apple-darwin/release/liblibrustdesk.dylib
- 官方 dylib 备份：~/rd_mc/liblibrustdesk_official_backup.dylib
- 部署/恢复一键脚本：goke-re/deploy_patched_mac.sh（patch|restore，建议用户在场时执行 patch）
