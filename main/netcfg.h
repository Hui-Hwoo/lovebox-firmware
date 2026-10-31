#pragma once
#include <stdbool.h>

// 系统级持久化配置(存 NVS)。画面各自的设置见 cfg_store(按画面命名空间)。
typedef struct {
    char ssid[33];
    char pass[65];
    char screen[16];   // 当前画面 id(如 "clockwx")
} lb_cfg_t;

extern lb_cfg_t g_cfg;       // 当前生效配置
extern char     g_ip[16];    // 当前 IP(STA 模式),未连为 ""

extern char g_ota_status[80];              // 最近一次 GitHub 检查结果(app_main 定义)
void trigger_update_check(void);           // 触发立即检查更新(app_main 定义)
void motor_spin(int times);                // 电机转动反馈(app_main 定义)

void netcfg_load(void);                    // 从 NVS 读取,缺省用编译默认
void netcfg_save(const lb_cfg_t *c);       // 写回 NVS
void netcfg_start_web(void);               // 启动设置网页服务器(80端口)
