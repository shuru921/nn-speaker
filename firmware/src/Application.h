#ifndef _application_h_
#define _application_h_

#include "state_machine/States.h"
#include "state_machine/RecogniseCommandState.h"

class I2SSampler;
class IndicatorLight;
class Speaker;

class Application
{
private:
    State *m_detect_wake_word_state;
    RecogniseCommandState *m_recognise_command_state;
    State *m_current_state;
    Speaker *m_speaker;

public:
    Application(I2SSampler *sample_provider, Speaker *speaker, IndicatorLight *indicator_light);
    ~Application();
    void begin();
    void run();
};

#endif
