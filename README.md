# Lovebox 自定义固件 (ESP32 / ILI9341)

把第一代 **Lovebox**(型号 LB003 / 主板 LovePix-MB Rev C,ESP32-WROOM-32E + 2.8″ 320×240 ILI9341 TFT)
从原厂云固件改造成**自托管、独立运行**的 Wi-Fi 仪表盘:大时钟 + 日期 + 天气(图标)。
完全脱离 loveboxlove.com 云,内容自己掌控,支持 OTA 自我更新与自我恢复。

> 本仓库公开,**不含任何机密**。设备真实 Wi-Fi 账号密码存在设备 NVS(通过网页设置),不在代码里。

**本文件是完整交付文档**:一位新工程师只读本仓库文档(README + RECOVERY.md),就能在任意机器上编译、更新、调试、恢复本设备。

---

## 目录
1. [它是什么 / 能做什么](#一它是什么)
2. [硬件](#二硬件已验证)
3. [仓库结构](#三仓库结构)
4. [从零搭建开发环境](#四从零搭建开发环境任意机器)
5. [编译](#五编译)
6. [更新固件的四种方式](#六更新固件的四种方式)
7. [设备访问与设置页](#七设备访问与设置页)
8. [无串口如何调试](#八无串口如何调试重要)
9. [恢复 / 兜底](#九恢复--兜底)
10. [版本管理](#十版本管理)
11. [架构要点与踩坑](#十一架构要点与踩坑血泪)

---

## 一、它是什么

- **独立运行**:SNTP 自己对时 + [open-meteo](https://open-meteo.com) 免费天气(HTTP),不依赖任何本地服务器或原厂云。
- **仪表盘**:大时钟(逐位刷新)、日期、分隔线、温度(带度数符号)+ **天气图标**、城市、底部 IP、左下角版本号、右下角 Claude 图标。灰阶分层。
- **网页配置**:Wi-Fi 账号密码、城市(英文名,自动地理编码取坐标)、温度单位(F/C)。
- **常驻配网热点**:连上家里网后仍广播 `Lovebox-Setup` 热点(WPA2),随时可访问设置页/传固件。
- **三路固件更新** + **离线恢复**(见第六、九节)。
- **坏固件自动回滚**:双 OTA 分区 + 引导器回滚,稳定运行 90 秒才标记有效。
- **电机反馈**:升级开始转 1 下,完成转 3 下。开机即点屏(不黑屏等待)。

---

## 二、硬件(已验证)

| 功能 | GPIO | 说明 |
|---|---|---|
| TFT SCLK | 18 | SPI2 / VSPI |
| TFT MOSI | 23 | |
| TFT CS | 5 | |
| TFT DC | 26 | |
| TFT RST | 27 | |
| 背光 BL | 16 | 高电平亮 |
| 电机 | 32 | 高电平转 |

**显示屏初始化(关键参数,反复试出来的):** ILI9341,320×240 横屏。
`MADCTL(0x36)=0x60`(MV+MX,RGB)+ `INVON(0x21)`(本面板需反相,否则黑白颠倒),颜色直通无需补偿。
SPI 10 MHz,mode 0,像素 RGB565 高字节先发。字体:公有领域 `font8x8`(整数倍放大)。

- USB 口**只供电无数据**。
- UART 排针(VCC/TX/RX/EN/BOOT/GND)预留给将来串口烧录/备份。
- 芯片:ESP32-WROOM-32E;Secure Boot 关闭;双 OTA 分区(见 `partitions.csv`,各 ~1.9MB)。

---

## 三、仓库结构

```
main/
  app_main.c       内核:Wi-Fi(STA优先→APSTA)、OTA(GitHub/局域网)、健康回滚、电机、启动
  dashboard.c      仪表盘渲染 + SNTP + 天气(图标) + 地理编码 + 启动画面 + 配网屏
  display.c/.h     ILI9341 驱动(填充/文字/位图/画线/圆/天气图标用的图形原语)
  netcfg.c/.h      NVS 配置 + 设置网页(/save、固件上传 /ota、立即检查 /checkupdate)
  font8x8.h        8x8 点阵字体(公有领域)
  claude_icon.h    右下角 Claude 图标(由 SVG 栅格化,40x40 1bpp)
  lovebox_config.h 本地机密(不入库);从 lovebox_config.h.example 复制
  CMakeLists.txt   组件依赖
CMakeLists.txt     顶层;set(PROJECT_VER "x.y.z")(CI 用 tag 覆盖)
partitions.csv     分区表(nvs/otadata/双ota)
sdkconfig          ESP-IDF 配置(已入库,CI 与本地一致)
.github/workflows/release.yml   推 tag → 云端编译 → 发布 Release(make_latest)
RECOVERY.md        固件恢复层级(卡住/变砖时怎么救)
scripts/recover_via_ap.sh       离线"电脑接管"恢复脚本(macOS)
```

---

## 四、从零搭建开发环境(任意机器)

需要 **ESP-IDF v5.1.x**(本项目用 v5.1.4)。

**macOS / Linux:**
```sh
# 1) 安装 ESP-IDF(官方步骤的精简版)
mkdir -p ~/esp && cd ~/esp
git clone -b v5.1.4 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh esp32
# 2) 每个新终端都要 source 一次(进入 IDF 环境)
. ~/esp/esp-idf/export.sh

# 3) 取本项目
git clone https://github.com/Hui-Hwoo/lovebox-firmware.git ~/lovebox_fw
cd ~/lovebox_fw
cp main/lovebox_config.h.example main/lovebox_config.h   # 填入默认 Wi-Fi 等(见下)
```

`main/lovebox_config.h`(机密,不入库)字段:
```c
#define LB_WIFI_SSID   "默认WiFi名"       // 首次上电用;之后设备网页改,存NVS
#define LB_WIFI_PASS   "默认WiFi密码"
#define LB_SERVER_IP   "10.0.0.186"       // 局域网OTA服务器(可选,见第六节方式3)
#define LB_SERVER_PORT 8080
#define LB_OTA_POLL_S  30                 // 局域网轮询间隔(秒)
// LB_HEALTHY_TIMEOUT_S / LB_FW_VERSION 为旧字段,保留即可
```
> 说明:编译进去的 Wi-Fi 只是**首次上电/NVS 为空时**的默认值。设备一旦用网页配过,就一直用 NVS 里的。
> 所以 CI 用占位默认(`lovebox_config.h.example`)编译出的固件,装到已配置过的设备上照样连得上。

**没有编译环境的机器也能更新**:直接发 GitHub tag 让云端编译(第六节方式 1),或上传已有的 `firmware.bin`(方式 2)。

---

## 五、编译

```sh
. ~/esp/esp-idf/export.sh          # 进入 IDF 环境
cd ~/lovebox_fw
idf.py set-target esp32            # 仅首次
idf.py build                       # 产物:build/lovebox_fw.bin
```
确认版本:`grep PROJECT_VER CMakeLists.txt`(CI 发布时会被 tag 覆盖)。

---

## 六、更新固件的四种方式

设备地址:家里网 `http://10.0.0.208`(实际 IP 见屏幕左下或路由器);或连热点 `Lovebox-Setup`(密码 `lovebox888`)后 `http://192.168.4.1`。

### 方式 1 — GitHub 自动更新 ⭐ 主力(改完代码的标准流程)
```sh
git tag v1.0.4 && git push origin v1.0.4
```
GitHub Actions 云端用 ESP-IDF 编译,发布 Release(资产名固定 `firmware.bin`,并 `make_latest`)。
设备每 **5 分钟**查一次 GitHub,**版本号数字更高**才自动下载升级(设置网页可点"立即检查"免等)。
- 优点:任何电脑、任何地方、不需要本地编译环境。
- 注意:GitHub `/releases/latest` API 发布后有 **~4 分钟传播延迟**,设备可能短时间仍显示"已是最新"。

### 方式 2 — 浏览器 / 命令行上传 `/ota` ⭐ 最快最可靠 & 兜底
- 网页:设备地址 → 固件更新 → 选 `firmware.bin` → 上传并更新。
- 命令行:
  ```sh
  curl --http1.1 -H "Expect:" --data-binary @build/lovebox_fw.bin http://10.0.0.208/ota
  ```
  **必须加 `--http1.1 -H "Expect:"`**,否则 `esp_ota_end` 校验失败;第一次偶尔返回 500,重试即可。
- 离线、任何同网络设备都能传。

### 方式 3 — 局域网自动拉取(在家,~30 秒生效)
在服务器(如 Mac)上跑仓库里的小服务器,设备轮询 `http://<LB_SERVER_IP>:8080/fw/firmware.bin`:
```sh
idf.py build
python3 scripts/lan_serve.py build/lovebox_fw.bin   # 本机 IP 需等于 LB_SERVER_IP
```
改完重新 build,设备约 30 秒后自动拉取(版本不同即更新)。需设备在家里网。

### 方式 4 — 恢复/兜底
见第九节与 **RECOVERY.md**。

> **切勿同时**用多条 OTA 通道(并发写同一 OTA 分区会校验失败)。调试时建议只用一条。

---

## 七、设备访问与设置页

- 家里网:`http://10.0.0.208`(或屏幕左下角显示的 IP)。
- 设备热点:连 `Lovebox-Setup`(WPA2 密码 `lovebox888`)→ `http://192.168.4.1`。
  手机连热点会提示"无互联网",正常;本地页照常打开;不会自动弹窗,需手动输网址。
- 到新地方连不上已存 Wi-Fi 时,设备自动进配网模式(纯 AP),网页填新 Wi-Fi 保存即可。

设置页可改:**Wi-Fi SSID/密码、城市(英文名)、温度单位**,并可**立即检查 GitHub 更新**、**上传固件**。
页头显示:`固件版本 · 编译时间 · 运行分区`。

---

## 八、无串口如何调试(重要)

本设备 USB 无数据、暂无串口,**看不到串口日志**。调试全靠设备自己上报,看设置页(`http://10.0.0.208`):

- **版本 / 编译时间 / 分区**:页头直接显示。编译时间能区分这是 **CI 版**(时间≈发布时刻)还是**本地版**(你本地 build 时刻),以及跑在 `ota_0`/`ota_1` 哪个分区。
- **最近 GitHub 检查结果**:固件更新区的"最近检查"一行(`g_ota_status`),会显示:
  `已是最新 (x.y.z)` / `升级中 a→b` / `API连接失败 0xXXXX` / `下载失败 ...` / `API状态码 404` 等,是排查 GitHub 更新的主要窗口。点"立即检查"可手动触发。
- **设备是否在线**:`curl -s -o /dev/null -w "%{http_code}" http://10.0.0.208/`(200=在线)。
  ping 可能被忽略,以 HTTP 为准。macOS 的 ARP 有时陈旧,先扫一遍网段再查。
- **常见现象 → 判断**:
  - 屏幕显示 `Wi-Fi Setup` 配网屏 → 没连上家里网(新环境/密码错/信道问题),按第七节配网或等 5 分钟自动重启重试。
  - 开机黑屏约 1 分钟 → 旧版行为;现已"开机即点屏",若仍黑屏久说明启动卡住。
  - 版本莫名变回旧版 → 新固件在 90 秒内崩溃,引导器**回滚**到另一分区的上一个好版本(见第十一节)。
  - 升级后掉线连不回 → 历史上的 APSTA 信道冲突(已修),若复现见第十一节。

---

## 九、恢复 / 兜底

详见 **[RECOVERY.md](RECOVERY.md)**。层级(从省事到最后手段):
1. GitHub 自动拉(设备有网)→ 2. 局域网拉(在家)→ 3. 浏览器上传(手机最方便)
→ 4. **电脑接管离线脚本** `scripts/recover_via_ap.sh`(设备卡在热点、单网卡电脑连上即断外网时用)
→ 5. UART 串口(最后手段)。

坏固件若 90 秒内崩溃会**自动回滚**到上一个好版本。金标准好固件可留一份备份以便紧急上传。

---

## 十、版本管理

- 版本号 `主.次.补丁`(如 `1.0.3`),在顶层 `CMakeLists.txt` 的 `set(PROJECT_VER "...")`;**CI 发布时用 tag 覆盖**。
- 设备**只升级到版本号数字更高**的 Release,且只认 GitHub 标记的 **latest**(工作流已 `make_latest:true`)。
- 发版 = 打 tag 推上去(`git tag vX.Y.Z && git push origin vX.Y.Z`),CI 自动编译发布。
- 切换版本体系(如历史上从旧的 9.9.x 到 1.x)时,旧体系设备不会自动跨体系升级,需用方式 2/3 手动刷一次。

---

## 十一、架构要点与踩坑(血泪)

- **GitHub 下载的 302 重定向**:`github.com/.../releases/latest/download/firmware.bin` 会 302 跳到 CDN。
  `esp_https_ota` 和 `esp_http_client_perform` **都不会自动跟随**这个跨主机跳转(卡在 302)。
  本项目用 `esp_http_client_open` + `esp_http_client_set_redirection` **手动跟随重定向**,再流式 `esp_ota_write`;
  版本检查则用 GitHub API(`api.github.com/.../releases/latest`,直连无跳转,需带 User-Agent)。
- **APSTA 单天线信道冲突**:ESP32 只有一个射频,AP 和 STA 必须同信道。若开机就把热点固定在信道 1、
  而家里路由器在别的信道,STA 会连不上 → 卡在配网。**解法:开机先只开 STA 连家里网,连上后再把热点开在 STA 同信道**;
  连不上时用纯 AP 配网 + 每 5 分钟重启重试(每次开机都是"STA 优先")。
- **坏固件自动回滚**:`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` + 双 OTA 分区;`health_task` 跑满 **90 秒**才
  `esp_ota_mark_app_valid_cancel_rollback()`,所以 90 秒内崩溃/重启的固件会被引导器回滚到另一分区。
  **副作用**:若另一分区是旧版本,回滚会"变回旧版";保持两分区同代码可避免跨版本惊吓。
- **/ota 上传**:curl 必须 `--http1.1 -H "Expect:"`,否则 `esp_ota_end` 校验失败;首刷偶发 500 重试即可。
  当前运行的是 pending-verify 分区时 `esp_ota_set_boot_partition` 会报错,故 `/ota` 处理里会先 `mark_valid`。
- **显示**:MADCTL 只设 MV(0x20/0x28)是**对角转置**不是旋转(字会歪);BGR 位会红蓝对调(本屏是 RGB);
  INVOFF 会黑白反相。正确:`MADCTL=0x60 + INVON`,颜色直通。
- **macOS 恢复坑**:单网卡电脑连上设备热点即断外网(AI/远程会掉线);新版 macOS 用
  `networksetup -setairportnetwork` 按名连网络需给终端开"定位服务"权限。离线脚本见 RECOVERY.md。

---

🤖 本固件与文档由 [Claude Code](https://claude.com/claude-code) 协助逆向与编写。
