#pragma once
#include <stdbool.h>

// 时间服务:封装 SNTP 对时。任何画面都可使用,互不干扰。
void svc_time_start(void);   // 启动 SNTP(幂等,可重复调用)
bool svc_time_ready(void);   // 是否已至少对时成功一次
