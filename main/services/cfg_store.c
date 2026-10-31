// 通用按画面分区的配置存储(基于 NVS,每画面一个命名空间)。
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "cfg_store.h"

void cfg_get(const char *ns, const char *key, char *out, int outlen, const char *def) {
    if (outlen <= 0) return;
    out[0] = 0;
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) == ESP_OK) {
        size_t n = outlen;
        esp_err_t e = nvs_get_str(h, key, out, &n);
        nvs_close(h);
        if (e == ESP_OK && out[0]) return;
    }
    if (def) { strncpy(out, def, outlen-1); out[outlen-1] = 0; }
    else out[0] = 0;
}

void cfg_set(const char *ns, const char *key, const char *val) {
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, key, val ? val : "");
    nvs_commit(h);
    nvs_close(h);
}

bool cfg_has(const char *ns, const char *key) {
    nvs_handle_t h;
    bool has = false;
    if (nvs_open(ns, NVS_READONLY, &h) == ESP_OK) {
        size_t n = 0;
        if (nvs_get_str(h, key, NULL, &n) == ESP_OK && n > 1) has = true;  // n 含结尾 \0
        nvs_close(h);
    }
    return has;
}
