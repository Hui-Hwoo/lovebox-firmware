#pragma once
#include <stdbool.h>

// 通用按画面分区的配置存储:每个画面用自己的 NVS 命名空间(= 画面 id),
// 字段 key 在该命名空间内。用于画面各自的设置与 API 密钥。
// 密钥只写入、不回显(网页用 cfg_has 显示"已设置/未设置")。

void cfg_get(const char *ns, const char *key, char *out, int outlen, const char *def);
void cfg_set(const char *ns, const char *key, const char *val);
bool cfg_has(const char *ns, const char *key);   // 该 key 是否已存有非空值
