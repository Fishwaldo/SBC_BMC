#ifndef TARGET_H
#define TARGET_H

#define NUM_TARGETS 6

typedef struct {
    uint8_t channel;
    uint8_t duty;
    float temp;
    uint32_t rpm;
    float load;
    time_t lastUpdate;
} target_t;

esp_err_t StartTarget(void);
esp_err_t target_send_temp(uint8_t channel, float temp);
esp_err_t target_send_duty(uint8_t channel, uint8_t duty);
esp_err_t target_send_load(uint8_t channel, float load);
esp_err_t target_send_rpm(uint8_t channel, uint32_t rpm);

esp_err_t target_get_data(uint8_t channel, target_t *data);

#endif