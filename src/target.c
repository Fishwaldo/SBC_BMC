#include <stdio.h>
#include <string.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <esp_err.h>
#include <esp_log.h>
#include "target.h"
#include "fanconfig.h"
#include "pwm.h"

static const char* TAG = "Target";

SemaphoreHandle_t targetLock[NUM_TARGETS];

target_t targets[NUM_TARGETS] = {
    {0, 255, 0, 0, 0, 0},
    {1, 255, 0, 0, 0, 0},
    {2, 255, 0, 0, 0, 0},
    {3, 255, 0, 0, 0, 0},
    {4, 255, 0, 0, 0, 0},
    {5, 255, 0, 0, 0, 0},
};

void vTaskTarget(void* pvParameters);

QueueHandle_t xTargetQueue;

typedef enum {
    TARGET_SET_TEMP,
    TARGET_SET_DUTY,
    TARGET_SET_LOAD,
    TARGET_SET_RPM,
} target_cmd_t;

struct setTempEvent {
    uint8_t channel;
    float temp;
};

struct setDutyEvent {
    uint8_t channel;
    uint8_t duty;
};

struct setLoadEvent {
    uint8_t channel;
    float load;
};

struct setRPMEvent {
    uint8_t channel;
    uint32_t rpm;
};

typedef struct TargetMessage_t {
    target_cmd_t type;
    union {
        struct setTempEvent setTemp;
        struct setDutyEvent setDuty;
        struct setLoadEvent setLoad;
        struct setRPMEvent setRPM;
    } data;
} TargetMessage_t;



esp_err_t StartTarget(void) {
    ESP_LOGD(TAG, "Starting target");
    xTargetQueue = xQueueCreate(10, sizeof(TargetMessage_t));
    if (xTargetQueue == NULL) {
        ESP_LOGE(TAG, "Failed to create target queue");
        return ESP_FAIL;
    }
    for (int i = 0; i < NUM_TARGETS; i++) {
        targetLock[i] = xSemaphoreCreateMutex();
        if (targetLock[i] == NULL) {
            ESP_LOGE(TAG, "Failed to create target lock");
            return ESP_FAIL;
        }
    }
    xTaskCreate(vTaskTarget, "Target", 4096, NULL, 5, NULL);
    return ESP_OK;
}

esp_err_t target_send_temp(uint8_t channel, float temp) {
    //ESP_LOGD(TAG, "Setting temp for channel %d to %d", channel, temp);
    TargetMessage_t *msg;
    msg = malloc(sizeof(TargetMessage_t));
    if (msg == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for target message");
        return ESP_FAIL;
    }
    msg->type = TARGET_SET_TEMP;
    msg->data.setTemp.channel = channel;
    msg->data.setTemp.temp = temp;
    if (xQueueSend(xTargetQueue, &msg, 10) != pdPASS) {
        ESP_LOGE(TAG, "Failed to send message to target queue");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t target_send_duty(uint8_t channel, uint8_t duty) {
    //ESP_LOGD(TAG, "Setting duty for channel %d to %d", channel, duty);
    TargetMessage_t *msg;
    msg = malloc(sizeof(TargetMessage_t));
    if (msg == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for message");
        return ESP_FAIL;
    }
    msg->type = TARGET_SET_DUTY;
    msg->data.setDuty.channel = channel;
    msg->data.setDuty.duty = duty;
    if (xQueueSend(xTargetQueue, &msg, 10) != pdPASS) {
        ESP_LOGE(TAG, "Failed to send message to target queue");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t target_send_load(uint8_t channel, float load) {
    //ESP_LOGD(TAG, "Setting Load for channel %d to %f", channel, load);
    TargetMessage_t *msg;
    msg = malloc(sizeof(TargetMessage_t));
    if (msg == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for message");
        return ESP_FAIL;
    }
    msg->type = TARGET_SET_LOAD;
    msg->data.setLoad.channel = channel;
    msg->data.setLoad.load = load;
    if (xQueueSend(xTargetQueue, &msg, 10) != pdPASS) {
        ESP_LOGE(TAG, "Failed to send message to target queue");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t target_send_rpm(uint8_t channel, uint32_t rpm) {
    //ESP_LOGD(TAG, "Setting RPM for channel %d to %d", channel, rpm);
    TargetMessage_t *msg;
    msg = malloc(sizeof(TargetMessage_t));
    if (msg == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for message");
        return ESP_FAIL;
    }
    msg->type = TARGET_SET_RPM;
    msg->data.setRPM.channel = channel;
    msg->data.setRPM.rpm = rpm;
    if (xQueueSend(xTargetQueue, &msg, 10) != pdPASS) {
        ESP_LOGE(TAG, "Failed to send message to target queue");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t target_get_data(uint8_t channel, target_t *data) {
    if (channel > NUM_TARGETS) {
        ESP_LOGE(TAG, "Invalid channel");
        return ESP_FAIL;
    }
    if (xSemaphoreTake(targetLock[channel], 100) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take target lock");
        ESP_LOGI(TAG, "Holder %s", pcTaskGetName(xSemaphoreGetMutexHolder(targetLock[channel])));
        return ESP_FAIL;
    }
    memcpy(data, &targets[channel], sizeof(target_t));
    xSemaphoreGive(targetLock[channel]);
    return ESP_OK;
}


esp_err_t target_calc_duty(int channel) {
    if (channel >= NUM_TARGETS) {
        ESP_LOGE(TAG, "Channel %d is out of range", channel);
        return ESP_ERR_INVALID_ARG;
    }
    if (channelConfig[channel].enabled == false) {
        ESP_LOGI(TAG, "Channel %d is disabled", channel);
        return ESP_OK;
    }
    time(&targets[channel].lastUpdate);
    if (targets[channel].temp == 0 && targets[channel].duty != 255) {
        ESP_LOGI(TAG, "Channel %d temp is 0. Setting Full Duty", channel);
        targets[channel].duty = 255;
        ESP_ERROR_CHECK(pwm_set_duty(channel, targets[channel].duty));
        return ESP_OK;
    }
    if (targets[channel].temp < channelConfig[channel].lowTemp) {
        ESP_LOGI(TAG, "Channel %d is below low temp", channel);
        if (targets[channel].duty != 0) {
            ESP_LOGI(TAG, "Setting channel %d to 0", channel);
            targets[channel].duty = 0;
            ESP_ERROR_CHECK(pwm_set_duty(channel, targets[channel].duty));
        }
        return ESP_OK;
    }
    if (targets[channel].temp > channelConfig[channel].highTemp) {
        ESP_LOGI(TAG, "Channel %d is above high temp", channel);
        if (targets[channel].duty != 255) {
            ESP_LOGI(TAG, "Setting channel %d to 255", channel);
            targets[channel].duty = 255;
            ESP_ERROR_CHECK(pwm_set_duty(channel, targets[channel].duty));
        }
        return ESP_OK;
    }
    uint32_t tempRange = channelConfig[channel].highTemp - channelConfig[channel].lowTemp;
    uint32_t tempOffset = targets[channel].temp - channelConfig[channel].lowTemp;
    uint8_t duty = (uint8_t) (tempOffset * 255 / tempRange);
    if (duty < channelConfig[channel].minDuty) {
        ESP_LOGI(TAG, "Channel %d is below min duty - %d", channel, duty);
        duty = channelConfig[channel].minDuty;
    }
    if (duty == targets[channel].duty) {
        ESP_LOGD(TAG, "Channel %d duty is unchanged - %d", channel, duty);
        return ESP_OK;
    }
    targets[channel].duty = duty;
    ESP_LOGD(TAG, "Calculated duty for channel %d to %d", channel, targets[channel].duty);
    ESP_ERROR_CHECK(pwm_set_duty(channel, duty));
    return ESP_OK;
}

void vTaskTarget(void* pvParameters) {
    TargetMessage_t *msg;
    ESP_LOGD(TAG, "Starting target task");
    for (;;) {
        if ( xQueueReceive( xTargetQueue, &(msg), ( TickType_t ) 1000 / portTICK_PERIOD_MS ) == pdPASS ) {
            //ESP_LOGD(TAG, "Received message of type %d", msg->type);
            switch (msg->type) {
                case TARGET_SET_TEMP:
                    if (msg->data.setTemp.channel >= NUM_TARGETS) {
                        ESP_LOGE(TAG, "SetTemp: Channel %d is out of range", msg->data.setTemp.channel);
                        break;
                    }
                    if (channelConfig[msg->data.setTemp.channel].enabled == false) {
                        ESP_LOGE(TAG, "SetTemp: Channel %d is disabled", msg->data.setTemp.channel);
                        break;
                    }
                    if (xSemaphoreTake(targetLock[msg->data.setTemp.channel], portMAX_DELAY) == pdFALSE) {
                        ESP_LOGE(TAG, "SetTemp: Failed to take target lock");
                        break;
                    }
                    ESP_LOGD(TAG, "Setting temp for channel %d to %f", msg->data.setTemp.channel, msg->data.setTemp.temp);
                    targets[msg->data.setTemp.channel].temp = msg->data.setTemp.temp;
                    targets[msg->data.setTemp.channel].lastUpdate = time(NULL);
                    ESP_ERROR_CHECK(target_calc_duty(msg->data.setTemp.channel));
                    xSemaphoreGive(targetLock[msg->data.setTemp.channel]);
                    break;
                case TARGET_SET_DUTY:
                    if (msg->data.setDuty.channel >= NUM_TARGETS) {
                        ESP_LOGE(TAG, "SetDuty: Channel %d is out of range", msg->data.setDuty.channel);
                        break;
                    }
                    if (channelConfig[msg->data.setDuty.channel].enabled == false) {
                        ESP_LOGE(TAG, "SetDuty: Channel %d is disabled", msg->data.setDuty.channel);
                        break;
                    }
                    if (xSemaphoreTake(targetLock[msg->data.setDuty.channel], portMAX_DELAY) == pdFALSE) {
                        ESP_LOGE(TAG, "SetDuty: Failed to take target lock");
                        break;
                    }
                    ESP_LOGD(TAG, "Setting duty for channel %d to %d", msg->data.setDuty.channel, msg->data.setDuty.duty);
                    targets[msg->data.setDuty.channel].duty = msg->data.setDuty.duty;
                    ESP_ERROR_CHECK(pwm_set_duty(msg->data.setDuty.channel, targets[msg->data.setDuty.channel].duty));
                    xSemaphoreGive(targetLock[msg->data.setDuty.channel]);
                    break;
                case TARGET_SET_LOAD:
                    if (msg->data.setLoad.channel >= NUM_TARGETS) {
                        ESP_LOGE(TAG, "SetLoad: Channel %d is out of range", msg->data.setLoad.channel);
                        break;
                    }
                    if (channelConfig[msg->data.setLoad.channel].enabled == false) {
                        ESP_LOGE(TAG, "SetLoad: Channel %d is disabled", msg->data.setLoad.channel);
                        break;
                    }
                    if (xSemaphoreTake(targetLock[msg->data.setLoad.channel], portMAX_DELAY) == pdFALSE) {
                        ESP_LOGE(TAG, "SetLoad: Failed to take target lock");
                        break;
                    }
                    ESP_LOGD(TAG, "Setting Load for channel %d to %f", msg->data.setLoad.channel, msg->data.setLoad.load);
                    targets[msg->data.setLoad.channel].load = msg->data.setLoad.load;
                    xSemaphoreGive(targetLock[msg->data.setLoad.channel]);
                    break;
                case TARGET_SET_RPM:
                    if (msg->data.setRPM.channel >= NUM_TARGETS) {
                        ESP_LOGE(TAG, "setRPM: Channel %d is out of range", msg->data.setRPM.channel);
                        break;
                    }
                    if (channelConfig[msg->data.setRPM.channel].enabled == false) {
                        ESP_LOGE(TAG, "setRPM: Channel %d is disabled", msg->data.setRPM.channel);
                        break;
                    }
                    if (xSemaphoreTake(targetLock[msg->data.setRPM.channel], portMAX_DELAY) == pdFALSE) {
                        ESP_LOGE(TAG, "setRPM: Failed to take target lock");
                        break;
                    }
                    ESP_LOGD(TAG, "Setting RPM for channel %d to %d", msg->data.setRPM.channel, msg->data.setRPM.rpm);
                    targets[msg->data.setRPM.channel].rpm = msg->data.setRPM.rpm;
                    xSemaphoreGive(targetLock[msg->data.setRPM.channel]);
                    break;
            }
            free(msg);
        } else {
            continue;
            ESP_LOGD(TAG, "Checking for Stale Data"); 
            for (int i = 0; i < NUM_TARGETS; i++) {
                if (channelConfig[i].enabled == false) {
                    continue;
                }
                if (xSemaphoreTake(targetLock[i], portMAX_DELAY) == pdFALSE) {
                    ESP_LOGE(TAG, "Stale Data: Failed to take target lock");
                    continue;
                }
                if (targets[i].lastUpdate == 0) {
                    ESP_LOGI(TAG, "Channel %d has not been updated", i);
                    pwm_set_duty(i, 255);
                    xSemaphoreGive(targetLock[i]);
                    continue;
                }
                time_t now;
                time(&now);
                if (now - targets[i].lastUpdate > 5) {
                    ESP_LOGI(TAG, "Channel %d has timed out", i);
                    pwm_set_duty(i, 255);
                    xSemaphoreGive(targetLock[i]);
                    continue;
                }
                xSemaphoreGive(targetLock[i]);
            }
        }
    }
    ESP_LOGW(TAG, "Target task exiting");
}