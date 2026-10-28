#!/bin/bash
# Lovebox 离线恢复(macOS):当设备卡在 Lovebox-Setup 配网热点、GitHub/局域网都够不到时,
# 把 Mac 的 Wi-Fi 切到设备热点、上传固件、再切回家里网。全程本地运行,不需要外网
# (所以 Claude/AI 掉线也没关系)。跑完看 recovery.log。
#
#   用法:普通终端里运行(不要在 Claude 里跑)
#     bash scripts/recover_via_ap.sh [firmware.bin路径]
#
# ⚠️ 新版 macOS 需先给终端 App 开"定位服务"(系统设置→隐私与安全性→定位服务→Terminal),
#    否则 networksetup 按名字连网络会报"找不到网络"。
#
set +e
IF="${LOVEBOX_IF:-en0}"                       # Wi-Fi 接口
AP_SSID="${LOVEBOX_AP_SSID:-Lovebox-Setup}"
AP_PASS="${LOVEBOX_AP_PASS:-lovebox888}"
DEV_AP_IP="192.168.4.1"
FW="${1:-build/lovebox_fw.bin}"
LOG="recovery.log"

exec > >(tee "$LOG") 2>&1
echo "========== [$(date)] Lovebox 离线恢复 =========="
[ -f "$FW" ] || { echo "找不到固件: $FW(传参或先 idf.py build)"; exit 1; }
echo "固件: $FW ($(stat -f%z "$FW" 2>/dev/null || stat -c%s "$FW") 字节),接口: $IF"

echo "--- 确保 Wi-Fi 打开 ---"
networksetup -setairportpower "$IF" on; sleep 3

echo "--- 连接设备热点 $AP_SSID ---"
networksetup -setairportnetwork "$IF" "$AP_SSID" "$AP_PASS"
sleep 8

echo "--- 等待 $DEV_AP_IP 可达(最多30秒)---"
reachable=0
for i in $(seq 1 15); do
  curl -s -o /dev/null --max-time 3 "http://$DEV_AP_IP/" && { reachable=1; echo "设备网页可达"; break; }
  sleep 2
done
[ $reachable = 0 ] && echo "⚠️ 够不到 $DEV_AP_IP:热点没连上(多半是定位权限,见脚本顶部)或设备热点未广播。"

echo "--- 上传固件(禁用 Expect,重试3次)---"
ok=0
for t in 1 2 3; do
  code=$(curl -s --max-time 120 --http1.1 -H "Expect:" -H "Content-Type: application/octet-stream" \
         --data-binary @"$FW" "http://$DEV_AP_IP/ota" -w '%{http_code}' -o /tmp/lovebox_ota_resp.txt)
  echo "第${t}次上传 HTTP=$code 响应=$(cat /tmp/lovebox_ota_resp.txt 2>/dev/null)"
  [ "$code" = "200" ] && { ok=1; break; }
  sleep 3
done
[ $ok = 1 ] && echo ">>> ✅ 上传成功,设备正在刷入并重启" || echo ">>> ❌ 上传失败(见上方原因)"

echo "--- 切回家里网(关开 Wi-Fi,让系统用钥匙串自动重连)---"
networksetup -setairportpower "$IF" off; sleep 3
networksetup -setairportpower "$IF" on;  sleep 8
echo "若没自动连回家里网,手动在 Wi-Fi 菜单点一下家里网络即可。"
echo "========== [$(date)] 结束 =========="
