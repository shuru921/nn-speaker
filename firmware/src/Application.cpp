#include <Arduino.h>
#include "Application.h"
#include "state_machine/DetectWakeWordState.h"
#include "IndicatorLight.h"
#include "Speaker.h"

Application::Application(I2SSampler *sample_provider, Speaker *speaker, IndicatorLight *indicator_light)
{
    m_speaker = speaker;
    m_detect_wake_word_state  = new DetectWakeWordState(sample_provider);
    m_recognise_command_state = new RecogniseCommandState(
        sample_provider, indicator_light, speaker);

    m_current_state = m_detect_wake_word_state;
}

void Application::begin()
{
    m_current_state->enterState();
    Serial.println("Ready - waiting for wake word...");
}

void Application::run()
{
    bool state_done = m_current_state->run();
    if (state_done)
    {
        m_current_state->exitState();

        if (m_current_state == m_detect_wake_word_state)
        {
            m_speaker->playOK();
            m_current_state = m_recognise_command_state;
        }
        else
        {
            m_current_state = m_detect_wake_word_state;
        }

        m_current_state->enterState();
    }
    vTaskDelay(10);
}
