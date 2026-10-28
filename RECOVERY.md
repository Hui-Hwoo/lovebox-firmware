# 固件恢复 / 兜底指南

当设备刷了坏固件、或卡在配网热点连不回家里网时,按下面的层级恢复。
**先理解设备此刻处于哪种状态**(看屏幕):

- 正常仪表盘 → 设备在线,直接用方法 1/2/3 推新固件。
- `Wi-Fi Setup / Lovebox-Setup / 192.168.4.1` 配网屏 → 连不上家里网,用方法 3/4。
- 黑屏/花屏/不停重启 → 可能坏固件,等**自动回滚**(见文末),或用方法 4。

设备热点:SSID `Lovebox-Setup`,WPA2 密码 `lovebox888`,设备地址 `http://192.168.4.1`。

---

## 方法 1 — GitHub 自动拉取(设备有网时,最省事)

```sh
git tag v9.9.XXX && git push origin v9.9.XXX
```
GitHub Actions 云端编译并发布 Release。设备每 5 分钟查一次,版本更高就自动装。
在设置网页点"立即检查"可免等。**要求设备能上网**(家里网或任何有外网的 Wi-Fi)。

## 方法 2 — 局域网拉取(设备在家里网)

本地放一个 HTTP 服务供 `firmware.bin`,设备轮询它(默认 `http://<server>:8080/fw/firmware.bin`)。
把新 `build/lovebox_fw.bin` 拷到服务目录,设备约 30 秒后自动装。**要求设备在家里网。**

## 方法 3 — 浏览器上传(手机最方便,离线也行)⭐ 当前最推荐

设备卡在配网屏时用这个,**不依赖电脑,不碰 Wi-Fi 命令行**:

1. **手机用正常网络(蜂窝/家里 Wi-Fi)** 打开并下载固件:
   `https://github.com/Hui-Hwoo/lovebox-firmware/releases/latest/download/firmware.bin`
2. 手机 Wi-Fi 连 `Lovebox-Setup`(密码 `lovebox888`)。
3. 手机浏览器打开 `http://192.168.4.1` → 「固件更新」→ 选刚下载的 `firmware.bin` → 上传并更新。
4. 设备刷入并重启,自己连回家里网。手机再切回家里 Wi-Fi。

> 也可以用电脑做同样的事,但**单网卡电脑连上 `Lovebox-Setup` 就会断外网**(见方法 4 的坑)。

## 方法 4 — 电脑接管(脚本,离线)/ 或带第二路网的在线接管

适合"换新电脑让 AI 接管"的场景。脚本:`scripts/recover_via_ap.sh`。

**两个 macOS 现实限制(重要):**
- **单网卡**:Mac 只有 Wi-Fi 时,一连上 `Lovebox-Setup` 就断外网,AI(Claude)会掉线。
  → 脚本设计成**本地离线独立运行**(切热点→上传→切回),跑完看 `recovery.log`。
- **定位权限**:新版 macOS 下 `networksetup -setairportnetwork` 按名字连网络**需要给终端 App 开"定位服务"**
  (系统设置 → 隐私与安全性 → 定位服务 → 打开 Terminal/iTerm)。否则会报"找不到网络 / HTTP 000"。

**离线脚本用法**(普通终端,不要在 Claude 里跑):
```sh
bash scripts/recover_via_ap.sh [firmware.bin路径]    # 默认用 build/lovebox_fw.bin
```
它会:确保 Wi-Fi 开 → 连 `Lovebox-Setup` → 上传固件 → 用"关开 Wi-Fi"让系统凭钥匙串自动连回家里网。

**不掉线的在线接管(推荐,若有第二路网):**
给 Mac 加第二条外网(插网线,**或** iPhone 用数据线连 Mac 开「个人热点 / USB」),
这样 Wi-Fi 腾出来连 `Lovebox-Setup` 而 Mac 仍有外网 → AI 全程在线,可直接:
```sh
curl --http1.1 -H "Expect:" --data-binary @build/lovebox_fw.bin http://192.168.4.1/ota
```
(用 GUI 的 Wi-Fi 菜单手动连 `Lovebox-Setup` 最稳,绕开定位权限问题。)

## 方法 5 — UART 串口烧录(最后手段)

需要 USB-UART 适配器(接 TX/RX/GND,BOOT 拉低进下载模式),用 `esptool.py` 烧录。
OTA 永远只写 app 分区、不碰引导器,所以几乎不会到这一步。

---

## /ota 上传的坑

- 用 curl 必须禁用 Expect 头:`--http1.1 -H "Expect:"`,否则 `esp_ota_end` 校验失败(浏览器 fetch 无此问题)。
- 第一次上传偶尔返回 500,重试即可。
- **切勿同时**用两条 OTA 通道(并发写 OTA 分区会校验失败)。

## 坏固件自动回滚

双 OTA 分区 + `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`。固件要稳定运行 **90 秒**才被标记有效
(`health_task`);若在此之前崩溃/重启,引导器**自动回退到上一个好版本**。所以"开机就崩"的坏固件能自救。
超过 90 秒才崩的,设备每次开机仍能活 90 秒,用方法 3/4 覆盖即可。
