#ifndef FANCONFIG_H
#define FANCONFIG_H

#include <stdio.h>
#include <esp_err.h>
#include "target.h"

#define DEF_LOW_TEMP 55
#define DEF_HIGH_TEMP 80
#define DEF_LOW_DUTY 10

typedef struct {
    bool enabled;
    uint32_t lowTemp;
    uint32_t highTemp;
    uint8_t minDuty;
} channelConfig_t;

channelConfig_t channelConfig[NUM_TARGETS];
SemaphoreHandle_t configMutex;

typedef struct {
    char tz[32];
    char username[32];
    char password[32];
    char agenttoken[9];
} deviceConfig_t;

deviceConfig_t deviceConfig;


esp_err_t StartConfig(void);
esp_err_t loadChannelConfig(uint8_t channel);
esp_err_t saveChannelConfig(uint8_t channel);
esp_err_t saveTZ(char *tz);

#endif
