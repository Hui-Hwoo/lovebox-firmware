# Lovebox 自定义固件 (ESP32 / ILI9341)

把第一代 Lovebox(LovePix-MB Rev C,ESP32-WROOM-32E + 2.8" 320×240 ILI9341 TFT)
改造成一个**自托管、独立运行**的 Wi-Fi 仪表盘:大时钟 + 日期 + 天气,完全脱离原厂云。

## 功能
- **独立运行**:SNTP 对时 + open-meteo 天气(HTTP),不依赖任何本地服务器。
- **网页配置**(设备 IP / `192.168.4.1`):Wi-Fi 账号密码、城市、温度单位。
- **自动配网兜底**:连不上已存 Wi-Fi 时自动开 `Lovebox-Setup` 热点。
- **三种更新方式**:浏览器上传 / 局域网自动拉 / GitHub Releases 自动拉(HTTPS)。

## 硬件接线(已验证)
TFT(ILI9341):`SCLK=18 MOSI=23 CS=5 DC=26 RST=27 BL=16`,SPI2/VSPI,10MHz。
`MADCTL=0x60`(MV+MX,横屏 RGB)+ `INVOFF`,颜色直通。电机 `GPIO32`(高电平转)。

## 构建
```sh
cp main/lovebox_config.h.example main/lovebox_config.h   # 填入默认 Wi-Fi 等
idf.py build                                              # ESP-IDF v5.1
```
`main/lovebox_config.h` 含机密,不入库(见 `.gitignore`)。

## 更新
- 浏览器:打开设备网页 → 固件更新 → 上传 `build/lovebox_fw.bin`。
- GitHub:发布新 Release(附 `firmware.bin`),设备每 2 小时自动拉取最新版。
