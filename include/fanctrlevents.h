#ifndef FANCTRLEVENTS_H
#define FANCTRLEVENTS_H

#include <esp_event.h>

#ifdef __cplusplus
extern "C" {
#endif


// Declare an event base
ESP_EVENT_DECLARE_BASE(TIME_EVENTS);        // declaration of the time events family

enum {                
    TIME_EVENT_SYNC,      
};



#ifdef __cplusplus
}
#endif

#endif