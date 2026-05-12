#ifndef _application_h_
#define _applicaiton_h_

#include "state_machine/States.h"
#include <driver/gpio.h>

class I2SSampler;
class I2SOutput;
class State;
class IndicatorLight;
class Speaker;
class IntentProcessor;

typedef enum {
    IDLE,       // 等待按鈕
    RECORDING,  // 錄音中
} AppState;

class Application
{
private:
    State *m_recognise_command_state;
    AppState m_app_state;
    Speaker *m_speaker;
    IndicatorLight *m_indicator_light;
    gpio_num_t m_button_pin;
    bool m_last_button_state;

public:
    Application(I2SSampler *sample_provider, IntentProcessor *intent_processor, Speaker *speaker, IndicatorLight *indicator_light);
    ~Application();
    void run();
};

#endif
