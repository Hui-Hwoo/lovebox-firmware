// 画面调度器:注册表 + 启动流程 + 运行循环 + 热切换。
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "display.h"
#include "svc_time.h"
#include "splash.h"
#include "netcfg.h"
#include "screen_manager.h"

static const char *T = "screens";

// ==== 画面注册表 ====
// 新增画面:在此 extern 一行,并加入 REG[] 数组。顺序即网页下拉顺序。
extern const screen_t scr_clockwx;
extern const screen_t scr_bigclock;

static const screen_t *REG[] = {
    &scr_clockwx,
    &scr_bigclock,
};
#define N_SCREENS ((int)(sizeof(REG)/sizeof(REG[0])))

static int s_cur  = 0;               // 当前画面下标
static volatile int s_next = -1;     // 待切换目标下标(-1 表示无请求)
static volatile bool s_reload = false; // 配置改动后请求重载当前画面

int screen_manager_count(void) { return N_SCREENS; }
const screen_t *screen_manager_get(int i) { return (i >= 0 && i < N_SCREENS) ? REG[i] : NULL; }
const char *screen_manager_current_id(void) { return REG[s_cur]->id; }

static int find_by_id(const char *id) {
    if (!id) return -1;
    for (int i = 0; i < N_SCREENS; i++) if (strcmp(REG[i]->id, id) == 0) return i;
    return -1;
}

const screen_t *screen_manager_find(const char *id) {
    int i = find_by_id(id);
    return i >= 0 ? REG[i] : NULL;
}

void screen_manager_select(const char *id) {
    int i = find_by_id(id);
    if (i < 0) { ESP_LOGW(T, "未知画面 id=%s,忽略", id ? id : "(null)"); return; }
    s_next = i;   // 由调度循环择机切换(不在 web 线程里直接操作屏幕)
}

void screen_manager_reload(void) { s_reload = true; }

void screen_manager_run(void) {
    display_init();

    // ---- 启动阶段:接着启动页的进度条,准备公共服务 + 首个画面数据 ----
    ui_progress_to(65, 6);
    svc_time_start();
    ui_progress_to(72, 6);

    // 选定开机画面:优先用 NVS 中记录的,找不到则第 0 个
    int saved = find_by_id(g_cfg.screen);
    s_cur = (saved >= 0) ? saved : 0;

    if (REG[s_cur]->prepare) REG[s_cur]->prepare();   // 阻塞式加载(地理编码/首拉天气等)
    ui_progress_to(90, 6);

    // 等首次对时(最多30秒),进度条在 90~99% 间缓慢爬动
    for (int i = 0; i < 30 && !svc_time_ready(); i++) {
        ui_progress_to(90 + (i * 9) / 30, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ui_progress_to(100, 4);
    vTaskDelay(pdMS_TO_TICKS(300));    // 让 100% 停留一瞬

    // ---- 切入首个画面 ----
    fill_screen(C_BLACK);
    REG[s_cur]->enter();
    ESP_LOGI(T, "启动画面: %s (%s)", REG[s_cur]->id, REG[s_cur]->name);

    // ---- 调度循环 ----
    while (1) {
        // 处理热切换请求
        int nx = s_next;
        if (nx >= 0 && nx != s_cur) {
            s_next = -1;
            if (REG[s_cur]->leave) REG[s_cur]->leave();
            s_cur = nx;
            if (REG[s_cur]->prepare) REG[s_cur]->prepare();   // 先备好数据(旧画面仍在屏上)
            fill_screen(C_BLACK);                             // 再清屏切入,避免中途留白
            REG[s_cur]->enter();
            ESP_LOGI(T, "切换画面 -> %s (%s)", REG[s_cur]->id, REG[s_cur]->name);
        } else if (nx >= 0) {
            s_next = -1;   // 请求的就是当前画面,忽略
        }

        // 配置改动:重载当前画面(重取数据 + 重绘),不切换画面
        if (s_reload) {
            s_reload = false;
            if (REG[s_cur]->prepare) REG[s_cur]->prepare();
            fill_screen(C_BLACK);
            REG[s_cur]->enter();
            ESP_LOGI(T, "重载画面: %s", REG[s_cur]->id);
        }

        REG[s_cur]->tick();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
