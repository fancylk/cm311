#!/system/bin/sh
cd /data/local/tmp
for v in 0 1 2 3 4 5 7 8 9 10; do
  kill -9 $(pidof goke_daemon_v3) 2>/dev/null
  echo $v > gk_pack.cfg
  setsid ./goke_daemon_v3 </dev/null > gp$v.log 2>&1 &
  sleep 2
  ./goke_client_v3 36 >/dev/null 2>&1
  sleep 1
  echo "==PACK=$v=="
  grep -hE "set_pack|pack_type now|frame source" gp$v.log 2>/dev/null | head -3
  kill -9 $(pidof goke_daemon_v3) 2>/dev/null
done
