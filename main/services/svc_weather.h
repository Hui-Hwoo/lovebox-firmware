#pragma once
#include <stdbool.h>

// 天气服务:城市名 -> 坐标(地理编码)+ 当前天气(open-meteo)。
// 画面只负责拿快照来显示,拉取节奏由画面决定,数据可被多个画面共享。
typedef struct {
    bool  ok;       // 是否已成功取到过天气
    float temp;     // 当前温度(单位同 unit)
    char  unit;     // 'F' / 'C'
    int   wcode;    // WMO 天气代码
} weather_t;

void svc_weather_geocode(const char *city);  // 城市名 -> 坐标(阻塞 HTTP)
void svc_weather_refresh(char unit);         // 按当前坐标拉取天气,unit='F'/'C'(阻塞 HTTP)
bool svc_weather_get(weather_t *out);        // 拷出最新快照;返回是否已有有效数据
