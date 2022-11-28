#ifndef PWM_H
#define PWM_H

#define LEDC_TEST_CH_NUM       (6)
#define LEDC_TEST_DUTY         (26000)
#define LEDC_TEST_FADE_TIME    (3000)

esp_err_t StartPWM(void);
esp_err_t pwm_set_duty(uint8_t channel, uint8_t duty);
uint8_t pwm_get_duty(uint8_t channel);

#endif