/* LEDC (LED Controller) fade example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <math.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <driver/ledc.h>
#include <esp_err.h>
#include <esp_log.h>
#include "pwm.h"

static const char* TAG = "PWM";

/*
 * About this example
 *
 * 1. Start with initializing LEDC module:
 *    a. Set the timer of LEDC first, this determines the frequency
 *       and resolution of PWM.
 *    b. Then set the LEDC channel you want to use,
 *       and bind with one of the timers.
 *
 * 2. You need first to install a default fade function,
 *    then you can use fade APIs.
 *
 * 3. You can also set a target duty directly without fading.
 *
 * 4. On ESP32, GPIO18/19/4/5 are used as the LEDC outputs:
 *              GPIO18/19 are from the high speed channel group
 *              GPIO4/5 are from the low speed channel group
 *
 *    On other targets, GPIO8/9/4/5 are used as the LEDC outputs,
 *    and they are all from the low speed channel group.
 *
 * 5. All the LEDC outputs change the duty repeatedly.
 *
 */
#define LEDC_HS_TIMER          LEDC_TIMER_0
#ifdef CONFIG_IDF_TARGET_ESP32
#define LEDC_HS_MODE           LEDC_HIGH_SPEED_MODE
#elif CONFIG_IDF_TARGET_ESP32S3
#define LEDC_HS_MODE           LEDC_LOW_SPEED_MODE
#endif
#define LEDC_HS_CH0_GPIO       (26)
#define LEDC_HS_CH0_CHANNEL    LEDC_CHANNEL_0
#define LEDC_HS_CH1_GPIO       (18)
#define LEDC_HS_CH1_CHANNEL    LEDC_CHANNEL_1
#define LEDC_HS_CH2_GPIO       (23)
#define LEDC_HS_CH2_CHANNEL    LEDC_CHANNEL_2
#define LEDC_HS_CH3_GPIO       (5)
#define LEDC_HS_CH3_CHANNEL    LEDC_CHANNEL_3
#define LEDC_HS_CH4_GPIO       (33)
#define LEDC_HS_CH4_CHANNEL    LEDC_CHANNEL_4
#define LEDC_HS_CH5_GPIO       (22)
#define LEDC_HS_CH5_CHANNEL    LEDC_CHANNEL_6


    /*
     * Prepare individual configuration
     * for each channel of LED Controller
     * by selecting:
     * - controller's channel number
     * - output duty cycle, set initially to 0
     * - GPIO number where LED is connected to
     * - speed mode, either high or low
     * - timer servicing selected channel
     *   Note: if different channels use one timer,
     *         then frequency and bit_num of these channels
     *         will be the same
     */
    ledc_channel_config_t ledc_channel[LEDC_TEST_CH_NUM] = {
        {
            .channel    = LEDC_HS_CH0_CHANNEL,
            .duty       = 0,
            .gpio_num   = LEDC_HS_CH0_GPIO,
            .speed_mode = LEDC_HS_MODE,
            .hpoint     = 0,
            .timer_sel  = LEDC_HS_TIMER,
            .flags.output_invert = 0
        },
        {
            .channel    = LEDC_HS_CH1_CHANNEL,
            .duty       = 0,
            .gpio_num   = LEDC_HS_CH1_GPIO,
            .speed_mode = LEDC_HS_MODE,
            .hpoint     = 0,
            .timer_sel  = LEDC_HS_TIMER,
            .flags.output_invert = 0
        },
        {
            .channel    = LEDC_HS_CH2_CHANNEL,
            .duty       = 0,
            .gpio_num   = LEDC_HS_CH2_GPIO,
            .speed_mode = LEDC_HS_MODE,
            .hpoint     = 0,
            .timer_sel  = LEDC_HS_TIMER,
            .flags.output_invert = 0
        },
        {
            .channel    = LEDC_HS_CH3_CHANNEL,
            .duty       = 0,
            .gpio_num   = LEDC_HS_CH3_GPIO,
            .speed_mode = LEDC_HS_MODE,
            .hpoint     = 0,
            .timer_sel  = LEDC_HS_TIMER,
            .flags.output_invert = 0
        },
        {
            .channel    = LEDC_HS_CH4_CHANNEL,
            .duty       = 0,
            .gpio_num   = LEDC_HS_CH4_GPIO,
            .speed_mode = LEDC_HS_MODE,
            .hpoint     = 0,
            .timer_sel  = LEDC_HS_TIMER,
            .flags.output_invert = 0
        },
        {
            .channel    = LEDC_HS_CH5_CHANNEL,
            .duty       = 0,
            .gpio_num   = LEDC_HS_CH5_GPIO,
            .speed_mode = LEDC_HS_MODE,
            .hpoint     = 0,
            .timer_sel  = LEDC_HS_TIMER,
            .flags.output_invert = 0
        },
    };


/*
 * This callback function will be called when fade operation has ended
 * Use callback only if you are aware it is being called inside an ISR
 * Otherwise, you can use a semaphore to unblock tasks
 */
static bool cb_ledc_fade_end_event(const ledc_cb_param_t *param, void *user_arg)
{
    portBASE_TYPE taskAwoken = pdFALSE;
    ESP_LOGI(TAG, "Fade event %d, channel: %d, duty: %d", param->event, param->channel, param->duty);
    if (param->event == LEDC_FADE_END_EVT) {
        ESP_LOGI(TAG, "LEDC fade end event. Channel: %d, duty: %d", param->channel, param->duty);
    }
    return (taskAwoken == pdTRUE);
}

esp_err_t StartPWM(void)
{
    int ch;

    /*
     * Prepare and set configuration of timers
     * that will be used by LED Controller
     */
    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_TIMER_8_BIT, // resolution of PWM duty
        .freq_hz = 5000,                      // frequency of PWM signal
        .speed_mode = LEDC_HS_MODE,           // timer mode
        .timer_num = LEDC_HS_TIMER,            // timer index
        .clk_cfg = LEDC_AUTO_CLK,              // Auto select the source clock
    };
    // Set configuration of timer0 for high speed channels
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Set LED Controller with previously prepared configuration
    for (ch = 0; ch < LEDC_TEST_CH_NUM; ch++) {
        ESP_LOGD(TAG, "Setting up LEDC Channel %d", ch);
        ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel[ch]));
    }

    // Initialize fade service.
    ESP_ERROR_CHECK(ledc_fade_func_install(0));
    ledc_cbs_t callbacks = {
        .fade_cb = cb_ledc_fade_end_event
    };

    for (ch = 0; ch < LEDC_TEST_CH_NUM; ch++) {
        ESP_LOGD(TAG, "Installing Fade Callback for Channel %d", ch);
        ESP_ERROR_CHECK(ledc_cb_register(ledc_channel[ch].speed_mode, ledc_channel[ch].channel, &callbacks, NULL));
    }
    ESP_LOGI(TAG, "LEDC PWM setup complete");
#if 0
    while (1) {
        printf("1. LEDC fade up to duty = %d\n", LEDC_TEST_DUTY);
        for (ch = 0; ch < LEDC_TEST_CH_NUM; ch++) {
            ledc_set_fade_with_time(ledc_channel[ch].speed_mode,
                    ledc_channel[ch].channel, LEDC_TEST_DUTY, LEDC_TEST_FADE_TIME);
            ledc_fade_start(ledc_channel[ch].speed_mode,
                    ledc_channel[ch].channel, LEDC_FADE_NO_WAIT);
        }

        for (int i = 0; i < LEDC_TEST_CH_NUM; i++) {
            xSemaphoreTake(counting_sem, portMAX_DELAY);
        }

        printf("2. LEDC fade down to duty = 0\n");
        for (ch = 0; ch < LEDC_TEST_CH_NUM; ch++) {
            ledc_set_fade_with_time(ledc_channel[ch].speed_mode,
                    ledc_channel[ch].channel, 0, LEDC_TEST_FADE_TIME);
            ledc_fade_start(ledc_channel[ch].speed_mode,
                    ledc_channel[ch].channel, LEDC_FADE_NO_WAIT);
        }

        for (int i = 0; i < LEDC_TEST_CH_NUM; i++) {
            xSemaphoreTake(counting_sem, portMAX_DELAY);
        }

        printf("3. LEDC set duty = %d without fade\n", LEDC_TEST_DUTY);
        for (ch = 0; ch < LEDC_TEST_CH_NUM; ch++) {
            ledc_set_duty(ledc_channel[ch].speed_mode, ledc_channel[ch].channel, LEDC_TEST_DUTY);
            ledc_update_duty(ledc_channel[ch].speed_mode, ledc_channel[ch].channel);
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);

        printf("4. LEDC set duty = 0 without fade\n");
        for (ch = 0; ch < LEDC_TEST_CH_NUM; ch++) {
            ledc_set_duty(ledc_channel[ch].speed_mode, ledc_channel[ch].channel, 0);
            ledc_update_duty(ledc_channel[ch].speed_mode, ledc_channel[ch].channel);
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
#endif
    for (ch = 0; ch < LEDC_TEST_CH_NUM; ch++) {
        ESP_LOGD(TAG, "Starting Fade for Channel %d", ch);
        ledc_set_duty(ledc_channel[ch].speed_mode, ledc_channel[ch].channel, pow(2, LEDC_TIMER_8_BIT) - 1);
        ledc_update_duty(ledc_channel[ch].speed_mode, ledc_channel[ch].channel);
    }
    return ESP_OK;
}

esp_err_t pwm_set_duty(uint8_t channel, uint8_t duty)
{
    if (channel >= LEDC_TEST_CH_NUM) {
        ESP_LOGW(TAG, "Invalid Channel %d", channel);
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = ledc_set_fade_with_time(ledc_channel[channel].speed_mode, ledc_channel[channel].channel, duty, LEDC_TEST_FADE_TIME);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ledc_set_fade_with_time failed: %d", err);
        return err;
    }
    ESP_LOGI(TAG, "Setting duty for channel %d to %d", channel, duty);
    err = ledc_update_duty(ledc_channel[channel].speed_mode, ledc_channel[channel].channel);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ledc_update_duty failed: %d", err);
        return err;
    }
    return ESP_OK;
}

uint8_t pwm_get_duty(uint8_t channel) 
{
    if (channel >= LEDC_TEST_CH_NUM) {
        return 0;
    }
    return ledc_get_duty(ledc_channel[channel].speed_mode, ledc_channel[channel].channel);
}