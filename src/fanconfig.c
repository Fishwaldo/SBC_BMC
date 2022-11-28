#include <stdio.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_err.h>
#include <esp_log.h>
#include "esp_system.h"
#include "nvs_flash.h"
#include "fanconfig.h"

static const char* TAG = "Config";

esp_err_t setTZ(const char* tz);

esp_err_t StartConfig(void) {
    nvs_handle_t fanConfigHandle;
    /* Initialize NVS partition */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* NVS partition was truncated
         * and needs to be erased */
        ESP_ERROR_CHECK(nvs_flash_erase());

        /* Retry nvs_flash_init */
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    configMutex = xSemaphoreCreateMutex();
    if (configMutex == NULL) {
        ESP_LOGE(TAG, "Failed to create config mutex");
        return ESP_FAIL;
    }
    for (uint8_t i = 0; i < NUM_TARGETS; i++) {
        ESP_ERROR_CHECK(loadChannelConfig(i));
    }
    if (xSemaphoreTake(configMutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take config mutex");
        return ESP_FAIL;
    }
    ret = nvs_open("fanconfig", NVS_READWRITE, &fanConfigHandle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error opening fanconfig: %d", ret);
        xSemaphoreGive(configMutex);
        return ret;
    }
    size_t tzSize = sizeof(deviceConfig.tz);
    if (nvs_get_str(fanConfigHandle, "timezone", deviceConfig.tz, &tzSize) == ESP_OK) {
        ESP_ERROR_CHECK(setTZ(deviceConfig.tz));
    } else {
        ESP_LOGI(TAG, "No timezone set - Setting to Asia/Singapore");
        ESP_ERROR_CHECK(setTZ("Asia/Singapore"));
        ESP_ERROR_CHECK(nvs_set_str(fanConfigHandle, "timezone", "Asia/Singapore"));
        snprintf(deviceConfig.tz, sizeof(deviceConfig.tz), "Asia/Singapore");
    }
    tzSize = sizeof(deviceConfig.username);
    if (nvs_get_str(fanConfigHandle, "username", deviceConfig.username, &tzSize) == ESP_OK) {
        ESP_LOGI(TAG, "Username: %s", deviceConfig.username);
    } else {
        ESP_LOGI(TAG, "No username set - Defaulting to admin");
        ESP_ERROR_CHECK(nvs_set_str(fanConfigHandle, "username", "admin"));
        snprintf(deviceConfig.username, sizeof(deviceConfig.username), "admin");
    }
    tzSize = sizeof(deviceConfig.password);
    if (nvs_get_str(fanConfigHandle, "password", deviceConfig.password, &tzSize) == ESP_OK) {
        ESP_LOGI(TAG, "Password Retrieved from nvs");
    } else {
        ESP_LOGI(TAG, "No password set - Defaulting to password");
        ESP_ERROR_CHECK(nvs_set_str(fanConfigHandle, "password", "password"));
        snprintf(deviceConfig.password, sizeof(deviceConfig.password), "password");
    }
    tzSize = sizeof(deviceConfig.agenttoken);
    if (nvs_get_str(fanConfigHandle, "agenttoken", deviceConfig.agenttoken, &tzSize) == ESP_OK) {
        ESP_LOGI(TAG, "Agent Token Retrieved from nvs");
    } else {
        ESP_LOGI(TAG, "No agent token set - Defaulting to agenttoken");
        ESP_ERROR_CHECK(nvs_set_str(fanConfigHandle, "agenttoken", "12345678"));
        snprintf(deviceConfig.agenttoken, sizeof(deviceConfig.agenttoken), "12345678");
    }

    nvs_close(fanConfigHandle);
    xSemaphoreGive(configMutex);
    return ESP_OK;
}

esp_err_t saveTZ(char *tz) {
    nvs_handle_t fanConfigHandle;
    if (xSemaphoreTake(configMutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take config mutex");
        return ESP_FAIL;
    }
    ESP_ERROR_CHECK(nvs_open("fanconfig", NVS_READWRITE, &fanConfigHandle));
    setTZ(tz);
    ESP_ERROR_CHECK(nvs_set_str(fanConfigHandle, "timezone", tz));
    snprintf(deviceConfig.tz, sizeof(deviceConfig.tz), tz);
    nvs_close(fanConfigHandle);
    xSemaphoreGive(configMutex);
    return ESP_OK;
}

esp_err_t loadChannelConfig(uint8_t channel) {
    nvs_handle_t my_handle;
    esp_err_t err;
    char key[15];

    if (channel >= NUM_TARGETS) {
        ESP_LOGE(TAG, "Channel %d is out of range", channel);
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(configMutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take config mutex");
        return ESP_FAIL;
    }


    ESP_LOGD(TAG, "Loading config for channel %d", channel);

    sprintf(key, "device-%d", channel);
    err = nvs_open(key, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        xSemaphoreGive(configMutex);
        return err;
    }

    uint8_t val;
    err = nvs_get_u8(my_handle, "enabled", &val);
    if (err == ESP_ERR_NVS_NOT_FOUND ) {
        channelConfig[channel].enabled = true;
    } else if (err != ESP_OK) {
        xSemaphoreGive(configMutex);
        return err;
    } else {
        channelConfig[channel].enabled = val > 0 ? true : false;
    }

    err = nvs_get_u32(my_handle, "lowTemp", &channelConfig[channel].lowTemp);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        channelConfig[channel].lowTemp = DEF_LOW_TEMP;
    } else if (err != ESP_OK) {
        xSemaphoreGive(configMutex);
        return err;
    } 

    err = nvs_get_u32(my_handle, "highTemp", &channelConfig[channel].highTemp);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        channelConfig[channel].highTemp = DEF_HIGH_TEMP;
    } else if (err != ESP_OK) {
        xSemaphoreGive(configMutex);
        return err;
    } 

    err = nvs_get_u8(my_handle, "minDuty", &channelConfig[channel].minDuty);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        channelConfig[channel].minDuty = DEF_LOW_DUTY;
    } else if (err != ESP_OK) {
        xSemaphoreGive(configMutex);
        return err;
    }

    nvs_close(my_handle);
    xSemaphoreGive(configMutex);
    return ESP_OK;
}

esp_err_t saveChannelConfig(uint8_t channel) {
    nvs_handle_t my_handle;
    esp_err_t err;
    char key[15];

    if (channel >= NUM_TARGETS) {
        ESP_LOGE(TAG, "Channel %d is out of range", channel);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGD(TAG, "Saving config for channel %d", channel);

    if (xSemaphoreTake(configMutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take config mutex");
        return ESP_FAIL;
    }

    sprintf(key, "device-%d", channel);
    err = nvs_open(key, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        xSemaphoreGive(configMutex);
        return err;
    }

    err = nvs_set_u8(my_handle, "enabled", channelConfig[channel].enabled);
    if (err != ESP_OK) {
        xSemaphoreGive(configMutex);
        return err;
    }

    err = nvs_set_u32(my_handle, "lowTemp", channelConfig[channel].lowTemp);
    if (err != ESP_OK) {
        xSemaphoreGive(configMutex);
        return err;
    }

    err = nvs_set_u32(my_handle, "highTemp", channelConfig[channel].highTemp);
    if (err != ESP_OK) {
        xSemaphoreGive(configMutex);        
        return err;
    }
    err = nvs_set_u8(my_handle, "minDuty", channelConfig[channel].minDuty);
    if (err != ESP_OK) { 
        xSemaphoreGive(configMutex);        
        return err;
    }

    err = nvs_commit(my_handle);
    if (err != ESP_OK) {
        xSemaphoreGive(configMutex);        
        return err;
    }

    nvs_close(my_handle);
    
    xSemaphoreGive(configMutex);

    return ESP_OK;
}