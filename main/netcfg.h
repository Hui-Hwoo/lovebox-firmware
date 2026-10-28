#pragma once
#include <stdbool.h>

// 持久化配置(存 NVS,可通过网页动态修改)
typedef struct {
    char ssid[33];
    char pass[65];
    char city[48];
    char unit;        // 'F' 或 'C'
} lb_cfg_t;

extern lb_cfg_t g_cfg;       // 当前生效配置
extern char     g_ip[16];    // 当前 IP(STA 模式),未连为 ""

void netcfg_load(void);                    // 从 NVS 读取,缺省用编译默认
void netcfg_save(const lb_cfg_t *c);       // 写回 NVS
void netcfg_start_web(void);               // 启动设置网页服务器(80端口)
