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

    pinMode(m_button_pin, INPUT_PULLUP);

    m_recognise_command_state = new RecogniseCommandState(
        sample_provider, indicator_light, speaker, intent_processor);

    Serial.println("Ready - hold button to record, release to send");
}

void Application::run()
{
    bool button_pressed = (digitalRead(m_button_pin) == LOW);

    if (m_app_state == IDLE)
    {
        // 按鈕剛按下：開始錄音
        if (button_pressed && !m_last_button_state)
        {
            Serial.println("Recording...");
            m_app_state = RECORDING;
            m_recognise_command_state->enterState();
        }
    }
    else if (m_app_state == RECORDING)
    {
        if (button_pressed)
        {
            // 按住中：持續收集音訊
            m_recognise_command_state->run();
        }
        else if (!button_pressed && m_last_button_state)
        {
            // 按鈕剛放開：送出並處理
            Serial.println("Button released - sending...");
            m_recognise_command_state->finish();
            m_recognise_command_state->exitState();
            m_app_state = IDLE;
            Serial.println("Ready for next command");
        }
    }

    m_last_button_state = button_pressed;
    vTaskDelay(10);
}
