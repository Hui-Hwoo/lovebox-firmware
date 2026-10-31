#pragma once

// 一个"画面"(dashboard)的统一接口。每个画面是一个独立模块,
// 实现下面这组回调,并导出一个 const screen_t 实例(见 scr_*.c)。
// 要新增画面:复制一个 scr_xxx.c,实现回调,然后在 screen_manager.c
// 的注册表里加一行 extern + 一个数组项即可,互不影响其它画面。
typedef struct {
    const char *id;        // 稳定标识(存 NVS 用),如 "clockwx"
    const char *name;      // 网页下拉显示名,如 "时钟+天气"

    // 可选:在启动页进度条阶段调用(屏幕尚未清空),用于阻塞式加载数据
    // (如地理编码、首次拉天气),以免清屏后留白等待。可为 NULL。
    void (*prepare)(void);

    // 切入本画面:此时屏幕已清空为黑,负责画静态元素并做首帧渲染。
    void (*enter)(void);

    // 周期调用(约 1Hz):刷新动态内容,只重画变化部分以避免闪烁。
    void (*tick)(void);

    // 可选:切出本画面时的清理。可为 NULL。
    void (*leave)(void);
} screen_t;
