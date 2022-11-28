#include <stdio.h>
#include <string.h>
#include <time.h>
#include <timezonedb_lookup.h>
#include <esp_system.h>
#include <esp_log.h>

static const char* TAG = "TZConfig";

extern "C" {

esp_err_t setTZ(char* tz) {
    const char *tzEnv = lookup_posix_timezone_tz(tz);
    if (tzEnv == NULL) {
        ESP_LOGE(TAG, "Timezone %s not found", tz);
        return ESP_OK;
    }
    ESP_LOGI(TAG, "Setting timezone to %s (%s)", tz, tzEnv);
    setenv("TZ", tz, 1);
    tzset();
    return ESP_OK;
}
}