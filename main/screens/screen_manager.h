#pragma once
#include "screen.h"

// 画面调度器:持有画面注册表,负责启动服务、运行当前画面、
// 以及网页请求的"热切换"(不重启)。

void screen_manager_run(void);                 // 启动服务 + 进入调度循环(不返回)

// 给网页用的查询/控制接口:
int         screen_manager_count(void);        // 画面总数
const screen_t *screen_manager_get(int i);     // 第 i 个画面(用于列出下拉项)
const screen_t *screen_manager_find(const char *id); // 按 id 查画面(找不到返回 NULL)
const char *screen_manager_current_id(void);   // 当前画面 id
void        screen_manager_select(const char *id); // 请求热切换到某画面(线程安全触发)
void        screen_manager_reload(void);       // 请求重载当前画面(配置改动后重取数据+重绘)
