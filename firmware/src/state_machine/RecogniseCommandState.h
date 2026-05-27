#ifndef _recognise_command_state_h_
#define _recognise_command_state_h_

#include "States.h"

class I2SSampler;
class IndicatorLight;
class Speaker;
class JetsonUploader;

class RecogniseCommandState : public State
{
private:
    I2SSampler *m_sample_provider;
    unsigned long m_start_time;
    unsigned long m_elapsed_time;
    int m_last_audio_position;

    IndicatorLight *m_indicator_light;
    Speaker *m_speaker;

    JetsonUploader *m_speech_recogniser;

public:
    RecogniseCommandState(I2SSampler *sample_provider, IndicatorLight *indicator_light, Speaker *speaker);
    void enterState();
    bool run();       // 持續收集音訊，不自動結束
    void finish();    // 按鈕放開時呼叫：送出並處理結果
    void exitState();
};

#endif
