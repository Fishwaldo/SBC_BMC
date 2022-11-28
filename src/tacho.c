#include <stdio.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <driver/pcnt.h>
#include "tacho.h"
#include "target.h"


static const char* TAG = "Tacho";
void vTaskTacho(void* pvParameters);

QueueHandle_t xTachoQueue;
esp_timer_handle_t tacho_timer;

typedef struct TimeEvent_t {
    uint8_t channel;
    int16_t count;
} TimeEvent_t;

uint32_t count = 0;

typedef struct pinmap_t {
    uint8_t pin;
} pinmap_t;

pinmap_t pinmap[] = {
    { 25 },
    { 32 },
    { 4  },
    { 0  },
    { 2  },
    { 21 }
};

uint8_t curChan = 0;

esp_err_t StartTacho() {
    ESP_LOGD(TAG, "Starting tacho");
    xTachoQueue = xQueueCreate(10, sizeof(TimeEvent_t));
    if (xTachoQueue == NULL) {
        ESP_LOGE(TAG, "Failed to create Tacho queue");
        return ESP_FAIL;
    }
//25, 32, 4, 0, 2, 
    pcnt_config_t pcnt_config = {
        .pulse_gpio_num = pinmap[curChan].pin,
        .ctrl_gpio_num = PCNT_PIN_NOT_USED,
        .channel = PCNT_CHANNEL_0,
        .unit = PCNT_UNIT_0,
        .pos_mode = PCNT_COUNT_DIS,
        .neg_mode = PCNT_COUNT_INC,
        .lctrl_mode = PCNT_MODE_KEEP,
        .hctrl_mode = PCNT_MODE_KEEP,
        .counter_h_lim = 0,
        .counter_l_lim = 0,
    };
    ESP_ERROR_CHECK(pcnt_unit_config(&pcnt_config));
    ESP_ERROR_CHECK(pcnt_set_filter_value(PCNT_UNIT_0, 1023));
    ESP_ERROR_CHECK(pcnt_filter_enable(PCNT_UNIT_0));
    ESP_ERROR_CHECK(pcnt_counter_pause(PCNT_UNIT_0));
    ESP_ERROR_CHECK(pcnt_counter_clear(PCNT_UNIT_0));

    xTaskCreate(vTaskTacho, "Tacho", 2048, NULL, 5, NULL);
    return ESP_OK;
}

void TachoCallback(void *arg) {
    TimeEvent_t event;
    ESP_ERROR_CHECK(pcnt_counter_pause(PCNT_UNIT_0));
    event.channel = 0;
    ESP_ERROR_CHECK(pcnt_get_counter_value(PCNT_UNIT_0, &event.count));
    if (xQueueSend(xTachoQueue, &event, 0) == pdFALSE) {
        ESP_LOGE(TAG, "Failed to send tacho event");
    }
}


void vTaskTacho(void* pvParameters) {
    ESP_LOGD(TAG, "Starting tacho");

    const esp_timer_create_args_t tacho_timer_args = {
        .callback = &TachoCallback,
        .name = "tacho"
    };

    ESP_ERROR_CHECK(esp_timer_create(&tacho_timer_args, &tacho_timer));

    ESP_ERROR_CHECK(esp_timer_start_once(tacho_timer, 200000));
    TimeEvent_t msg;
    for (;;) {
        if ( xQueueReceive( xTachoQueue, &(msg), ( TickType_t ) 1000 / portTICK_PERIOD_MS ) == pdPASS ) {
            if (msg.count > 0) {
                uint32_t rpm = (msg.count * 5 * 60)/2;
                ESP_LOGI(TAG, "Channel: %d RPM: %d - %d", curChan, rpm, msg.count);
                ESP_ERROR_CHECK(target_send_rpm(curChan, rpm));
            }
            curChan++;
            if (curChan > 5) {
                curChan = 0;
            }
            ESP_ERROR_CHECK(pcnt_set_pin(PCNT_UNIT_0, PCNT_CHANNEL_0, pinmap[curChan].pin, PCNT_PIN_NOT_USED));
            ESP_ERROR_CHECK(pcnt_counter_clear(PCNT_UNIT_0));
            ESP_ERROR_CHECK(pcnt_counter_resume(PCNT_UNIT_0));
            ESP_ERROR_CHECK(esp_timer_start_once(tacho_timer, 200000));
        }
    }
}