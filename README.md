# Lovebox 自定义固件 (ESP32 / ILI9341)

把第一代 **Lovebox**(型号 LB003 / 主板 LovePix-MB Rev C,ESP32-WROOM-32E + 2.8″ 320×240 ILI9341 TFT)
从原厂云固件改造成一个**自托管、独立运行**的 Wi-Fi 仪表盘:大时钟 + 日期 + 天气,
完全脱离 loveboxlove.com 云,所有内容自己掌控。

> 本仓库是公开的,**不含任何机密**。设备真实的 Wi-Fi 账号密码存在设备 NVS 里(通过网页设置),
> 不在代码里。编译用的默认值在 `main/lovebox_config.h`(本地文件,`.gitignore` 排除)。

---

## 功能

- **独立运行**:SNTP 自己对时 + [open-meteo](https://open-meteo.com) 免费天气(HTTP),不依赖任何本地服务器。
- **网页配置**:改 Wi-Fi 账号密码、城市(英文名,自动地理编码)、温度单位(F/C)。
- **常驻配网热点**:设备一直广播 `Lovebox-Setup` 热点(WPA2),随时可连上改设置 / 传固件。
- **三路固件更新**:GitHub Releases 自动拉取(HTTPS)/ 局域网自动拉取 / 浏览器上传。
- **坏固件兜底**:双 OTA 分区 + 引导器回滚;稳定运行 90 秒才标记有效,坏固件自动回退上一个好版本。
- **电机反馈**:升级开始转 1 下,完成转 3 下。
- **开机即点屏**:一上电显示启动画面,不黑屏等待。

---

## 硬件(已验证)

| 功能 | GPIO | 说明 |
|---|---|---|
| TFT SCLK | 18 | SPI2 / VSPI |
| TFT MOSI | 23 | |
| TFT CS | 5 | |
| TFT DC | 26 | |
| TFT RST | 27 | |
| 背光 BL | 16 | 高电平亮 |
| 电机 | 32 | 高电平转 |

**显示屏初始化(关键,反复试出来的):** ILI9341,320×240 横屏。
`MADCTL(0x36)=0x60`(MV+MX,RGB)+ `INVON(0x21)`(本面板需反相,否则黑白颠倒),颜色直通无需软件补偿。
SPI 10 MHz,mode 0。像素 RGB565 高字节先发。
> 踩过的坑:MADCTL 只设 MV(0x20/0x28)是**对角转置**不是旋转,字会歪;BGR 位会让红蓝对调(本屏是 RGB);
> INVOFF 会让黑白反相。字体是公有领域 `font8x8`(`main/font8x8.h`),整数倍放大。

USB 口只供电无数据。UART 排针(VCC/TX/RX/EN/BOOT/GND)预留给将来串口烧录/备份。

---

## 仓库结构

```
main/
  app_main.c       内核:Wi-Fi(STA优先)、OTA(GitHub/局域网)、健康回滚、电机
  dashboard.c      仪表盘渲染 + SNTP + 天气 + 启动画面 + 配网屏
  display.c/.h     ILI9341 驱动(填充/文字/位图/画线)
  netcfg.c/.h      NVS 配置 + 设置网页(含固件上传 /ota、立即检查 /checkupdate)
  font8x8.h        8x8 点阵字体(公有领域)
  claude_icon.h    右下角 Claude 图标(由 SVG 栅格化)
  lovebox_config.h 本地机密(不入库);复制自 lovebox_config.h.example
.github/workflows/release.yml   推 tag → 云端编译 → 发布 Release
RECOVERY.md        固件恢复层级(卡住/变砖时怎么救)
scripts/recover_via_ap.sh       离线"电脑接管"恢复脚本(macOS)
```

---

## 编译

需要 [ESP-IDF **v5.1.x**](https://docs.espressif.com/projects/esp-idf/en/v5.1.4/esp32/)。

```sh
cp main/lovebox_config.h.example main/lovebox_config.h   # 填入你的默认 Wi-Fi 等
idf.py set-target esp32        # 首次
idf.py build
# 产物:build/lovebox_fw.bin
```

版本号在 `CMakeLists.txt` 的 `set(PROJECT_VER "...")`;CI 会用 tag 覆盖它。采用 `主.次.补丁`(如 `1.0.0`)。

> 设备只升级到"版本号数字更高"的 release,且只认 GitHub 标记的 **latest**(工作流已设 `make_latest:true`)。
> 所以切换版本体系(如从旧的 9.9.x 到 1.x)时,旧体系设备不会自动跨体系升级,需局域网/浏览器手动刷一次。

---

## 发布 / 更新(推荐:GitHub Actions)

**最省事、换任何电脑都行:**

```sh
git tag v9.9.152 && git push origin v9.9.152
```

→ GitHub Actions 自动用 ESP-IDF 编译、发布 Release(资产名必须是 `firmware.bin`)。
设备每 **5 分钟**查一次 GitHub,**只在远端版本更高时**自动下载升级(可在设置网页点"立即检查"免等)。

> GitHub `/releases/latest` API 发布后有 **~4 分钟传播延迟**,设备可能短时间仍显示"已是最新"。

**其它两路更新:**
- **局域网**(在家):本地跑个 HTTP 服务供 `firmware.bin`,设备轮询 `http://<server>:8080/fw/firmware.bin`。
- **浏览器上传**:设置页 → 固件更新 → 选 `firmware.bin` → 上传(任何网络,离线也行)。

---

## Wi-Fi 与设置页

- **家里网内**:`http://<设备IP>`(IP 显示在屏幕左下角 / 路由器后台)。
- **设备热点**:连 `Lovebox-Setup`(WPA2 密码 `lovebox888`)→ 打开 `http://192.168.4.1`。
  - 连上热点时手机会提示"无互联网",正常,本地页面照样能开;不会自动弹窗,需手动输网址。
- 换到新地方连不上已存 Wi-Fi 时,设备自动进配网模式(纯 AP),在页面填新 Wi-Fi 保存即可。

设置页可改:Wi-Fi SSID/密码、城市(英文名)、温度单位,并可**立即检查 GitHub 更新**和**上传固件**。

---

## 恢复 / 兜底

见 **[RECOVERY.md](RECOVERY.md)**。一句话层级:
GitHub 自动拉 → 局域网拉 → 浏览器上传 → **离线电脑接管**(`scripts/recover_via_ap.sh`)→ UART 串口(最后手段)。
坏固件若 90 秒内崩溃会**自动回滚**到上一个好版本。

---

## 架构要点 / 踩坑

- **GitHub 下载的 302 重定向**:`github.com/.../releases/latest/download/firmware.bin` 会 302 跳到 CDN。
  `esp_https_ota` 和 `esp_http_client_perform` **都不会自动跟随**这个跨主机跳转(卡在 302)。
  必须用 `esp_http_client_open` + `esp_http_client_set_redirection` **手动跟随**,再流式 `esp_ota_write`。
  版本检查改用 GitHub API(`api.github.com/.../releases/latest`,直连无跳转)。
- **APSTA 单天线信道冲突**:ESP32 只有一个射频,AP 和 STA 必须同信道。若开机就把热点固定在信道 1,
  而家里路由器在别的信道,STA 会连不上 → 卡在配网。**解法:开机先只开 STA 连家里网,连上后再把热点开在同信道。**
- **坏固件回滚**:`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` + 双 OTA 分区;`health_task` 跑满 90 秒才
  `esp_ota_mark_app_valid_cancel_rollback()`,所以 90 秒内崩溃的固件会被引导器回滚。
- **/ota 上传**:用 curl 要禁用 Expect 头(`--http1.1 -H "Expect:"`),否则 `esp_ota_end` 校验失败;
  浏览器 fetch() 没这问题。**切勿同时**用两条 OTA 通道(并发写 OTA 分区会校验失败)。

---

🤖 本固件与文档由 [Claude Code](https://claude.com/claude-code) 协助逆向与编写。
