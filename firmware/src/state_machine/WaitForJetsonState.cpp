#include <Arduino.h>
#include "WaitForJetsonState.h"
#include "JetsonWSClient.h"
#include "TTSPlayer.h"
#include "IndicatorLight.h"
#include "../config.h"

WaitForJetsonState::WaitForJetsonState(JetsonWSClient *ws, TTSPlayer *tts, IndicatorLight *light)
    : m_ws(ws), m_tts(tts), m_light(light), m_enter_time(0)
{
}

void WaitForJetsonState::enterState()
{
    m_enter_time = millis();
    m_light->setState(PULSING); // pulse LED while waiting for Jetson
    Serial.println("WaitForJetsonState: waiting for Jetson response...");
}

bool WaitForJetsonState::run()
{
    // Timeout guard
    if (millis() - m_enter_time > JETSON_WS_TIMEOUT_MS)
    {
        Serial.println("WaitForJetsonState: timeout, giving up");
        return true;
    }

    if (!m_ws->hasResponse())
        return false; // still waiting

    // Got a response from Jetson
    String response = m_ws->takeResponse();
    Serial.printf("WaitForJetsonState: Jetson says → \"%s\"\n", response.c_str());

    m_light->setState(ON);

    // Speak the response via OpenAI TTS
    if (response.length() > 0)
        m_tts->speak(response);

    return true; // done, go back to wake-word detection
}

void WaitForJetsonState::exitState()
{
    m_light->setState(OFF);
}
