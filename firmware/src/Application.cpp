#include <Arduino.h>
#include "Application.h"
#include "state_machine/RecogniseCommandState.h"
#include "IndicatorLight.h"
#include "Speaker.h"
#include "IntentProcessor.h"
#include "config.h"

Application::Application(I2SSampler *sample_provider, IntentProcessor *intent_processor, Speaker *speaker, IndicatorLight *indicator_light)
{
    m_speaker = speaker;
    m_indicator_light = indicator_light;
    m_app_state = IDLE;
    m_last_button_state = false;
    m_button_pin = RECORD_BUTTON_PIN;

    // 設定按鈕為輸入（內部上拉）
    pinMode(m_button_pin, INPUT_PULLUP);

    // 建立錄音狀態
    m_recognise_command_state = new RecogniseCommandState(
        sample_provider, indicator_light, speaker, intent_processor);

    Serial.println("Ready - press button to record");
}

void Application::run()
{
    // 讀取按鈕（LOW = 按下，因為 PULLUP）
    bool button_pressed = (digitalRead(m_button_pin) == LOW);

    if (m_app_state == IDLE)
    {
        // 按鈕剛按下（偵測下降緣）
        if (button_pressed && !m_last_button_state)
        {
            Serial.println("Button pressed - start recording");
            m_app_state = RECORDING;
            m_recognise_command_state->enterState();
        }
    }
    else if (m_app_state == RECORDING)
    {
        bool done = m_recognise_command_state->run();
        if (done)
        {
            m_recognise_command_state->exitState();
            m_app_state = IDLE;
            Serial.println("Done - ready for next command");
        }
    }

    m_last_button_state = button_pressed;
    vTaskDelay(10);
}
