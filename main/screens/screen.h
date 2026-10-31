#pragma once

// 画面可声明的配置字段类型:普通文本 / 密钥(不回显)/ 下拉选择。
typedef enum { FLD_TEXT, FLD_SECRET, FLD_SELECT } fld_type_t;

// 一个配置字段的声明。画面在 fields[] 里列出自己需要的字段,
// 网页据此自动渲染表单;值存在该画面的 NVS 命名空间(= 画面 id)中。
typedef struct {
    const char *key;      // NVS key(也是表单字段名),如 "city" / "api_key"
    const char *label;    // 网页显示标签
    fld_type_t  type;     // 文本 / 密钥 / 下拉
    const char *def;      // 默认值(FLD_SECRET 一般为空)
    const char *options;  // 仅 FLD_SELECT:"值:标签,值:标签"(无冒号则值即标签)
} scr_field_t;

// 一个"画面"(dashboard)的统一接口。每个画面是一个独立模块,
// 实现下面这组回调,并导出一个 const screen_t 实例(见 scr_*.c)。
// 要新增画面:复制一个 scr_xxx.c,实现回调,然后在 screen_manager.c
// 的注册表里加一行 extern + 一个数组项即可,互不影响其它画面。
typedef struct {
    const char *id;        // 稳定标识(存 NVS 用),如 "clockwx"
    const char *name;      // 网页下拉显示名,如 "时钟+天气"

    const scr_field_t *fields;  // 本画面需要的配置字段(可为 NULL)
    int               n_fields; // 字段个数

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
