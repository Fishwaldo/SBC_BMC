
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_timer.h>
#ifdef CONFIG_IDF_TARGET_ESP32
#include <himem.h>
#endif
#include <esp_ghota.h>
#include "fanctrlevents.h"
#include "wifi.h"
#include "fanconfig.h"
#include "pwm.h"
#include "network.h"
#include "target.h"
#include "tacho.h"

static const char* TAG = "Main";

ESP_EVENT_DEFINE_BASE(TIME_EVENTS);

static void event_callback(void* handler_args, esp_event_base_t base, int32_t id, void* event_data) {
    ESP_LOGI(TAG, "Event received: %s:%d", base, id);
    if (base == GHOTA_EVENTS) {
        ESP_LOGI(TAG, "GHOTA event received: %s", ghota_get_event_str(id));
    }

}

esp_err_t StartOTATask() {
    /* initialize our ghota config */
    ghota_config_t ghconfig = {
        .filenamematch = "fanctrl-esp32.bin",
        //.storagenamematch = "storage-esp32.bin",
        //.storagepartitionname = "storage",
        /* 1 minute as a example, but in production you should pick something larger (remember, Github has ratelimites on the API! )*/
        .updateInterval = 60,
    };
    /* initialize ghota. */
    ghota_client_handle_t *ghota_client = ghota_init(&ghconfig);
    if (ghota_client == NULL) {
        ESP_LOGE(TAG, "ghota_client_init failed");
        return ESP_ERR_INVALID_ARG;
    }
    /* register for events relating to the update progress */
    esp_event_handler_register(GHOTA_EVENTS, ESP_EVENT_ANY_ID, &event_callback, ghota_client);
    ESP_ERROR_CHECK(ghota_start_update_timer(ghota_client));
    return ESP_OK;

}

void app_main() {
    esp_err_t err;
    /* Initialize the event loop */
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_event_handler_register(TIME_EVENTS, ESP_EVENT_ANY_ID, &event_callback, NULL));

    ESP_ERROR_CHECK(StartConfig());

    ESP_ERROR_CHECK(StartWIFI());
    ESP_LOGD(TAG, "WIFI started");

    err = StartNetwork();
    if (err) {
        ESP_LOGW(TAG, "Error starting network: %d", err);
    }
    ESP_LOGD(TAG, "Network started");

    ESP_ERROR_CHECK(StartPWM());
    ESP_LOGD(TAG, "PWM started");

    ESP_ERROR_CHECK(StartTarget());
    ESP_LOGD(TAG, "Target started");

    ESP_ERROR_CHECK(StartTacho());
    ESP_LOGD(TAG, "Tacho started");

    ESP_ERROR_CHECK(StartOTATask());
    ESP_LOGD(TAG, "OTA Background Task Started");

//    char pcbuf[1024];
    for (;;) {
        // ESP_LOGD(TAG, "Looping");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        //pwm_set_duty(1, 0);
//        vTaskList(pcbuf);
//        printf("%s", pcbuf);
//        printf("======================================================================\n");
        //esp_timer_dump(stdout);
        //heap_caps_print_heap_info(MALLOC_CAP_8BIT);
//        printf("Memory: Free %dKiB Low: %dKiB\n", (int)xPortGetFreeHeapSize()/1024, (int)xPortGetMinimumEverFreeHeapSize()/1024);
#ifdef CONFIG_IDF_TARGET_ESP32
//        printf("HighMemory: Total: %dKiB, Free: %dKiB\n", (int)esp_himem_get_phys_size()/1024, (int)esp_himem_get_free_size()/1024);
#endif
//        printf("======================================================================\n");
    }
}