#ifndef _wait_for_jetson_state_h_
#define _wait_for_jetson_state_h_

#include "States.h"
#include <Arduino.h>

class JetsonWSClient;
class TTSPlayer;
class IndicatorLight;

class WaitForJetsonState : public State
{
public:
    WaitForJetsonState(JetsonWSClient *ws, TTSPlayer *tts, IndicatorLight *light);

    void enterState() override;
    bool run() override;    // returns true when done (response received or timeout)
    void exitState() override;

private:
    JetsonWSClient *m_ws;
    TTSPlayer *m_tts;
    IndicatorLight *m_light;
    unsigned long m_enter_time;
};

#endif
