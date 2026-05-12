#include <Arduino.h>
#include <ArduinoJson.h>
#include "I2SSampler.h"
#include "RingBuffer.h"
#include "RecogniseCommandState.h"
#include "IndicatorLight.h"
#include "Speaker.h"
#include "IntentProcessor.h"
#include "JetsonUploader.h"
#include "../config.h"
#include <string.h>

// 最長錄音秒數（防止按住太久塞爆記憶體）
#define MAX_RECORD_SECONDS 10

RecogniseCommandState::RecogniseCommandState(I2SSampler *sample_provider, IndicatorLight *indicator_light, Speaker *speaker, IntentProcessor *intent_processor)
{
    m_sample_provider = sample_provider;
    m_indicator_light = indicator_light;
    m_speaker = speaker;
    m_intent_processor = intent_processor;
    m_speech_recogniser = NULL;
}

void RecogniseCommandState::enterState()
{
    m_indicator_light->setState(ON);
    m_speaker->playReady();
    m_last_audio_position = -1;
    m_speech_recogniser = new JetsonUploader(JETSON_IP, JETSON_PORT, MAX_RECORD_SECONDS);
    Serial.println("[ASR] Recording... release button to send");
}

// 持續收集音訊，不自動結束
bool RecogniseCommandState::run()
{
    if (!m_speech_recogniser) return true;

    if (m_last_audio_position == -1)
        m_last_audio_position = m_sample_provider->getCurrentWritePosition() - 16000;

    int audio_position = m_sample_provider->getCurrentWritePosition();
    int sample_count = (audio_position - m_last_audio_position + m_sample_provider->getRingBufferSize()) % m_sample_provider->getRingBufferSize();

    if (sample_count > 0)
    {
        RingBufferAccessor *reader = m_sample_provider->getRingBufferReader();
        reader->setIndex(m_last_audio_position);
        int16_t sample_buffer[500];
        int remaining = sample_count;
        while (remaining > 0)
        {
            int batch = min(remaining, 500);
            for (int i = 0; i < batch; i++)
            {
                sample_buffer[i] = reader->getCurrentSample();
                reader->moveToNextSample();
            }
            m_speech_recogniser->addSamples(sample_buffer, batch);
            remaining -= batch;
        }
        m_last_audio_position = reader->getIndex();
        delete reader;
    }
    return false; // 不自動結束，等 finish() 呼叫
}

// 按鈕放開時呼叫：送出音訊並處理結果
void RecogniseCommandState::finish()
{
    if (!m_speech_recogniser) return;

    m_indicator_light->setState(PULSING);
    Serial.println("[ASR] Sending to Jetson...");

    String text = m_speech_recogniser->sendAndGetText();
    Serial.printf("[ASR] Result: %s\n", text.c_str());

    Intent intent;
    intent.text = text.c_str();
    IntentResult intentResult = m_intent_processor->processIntent(intent);
    switch (intentResult)
    {
    case SUCCESS:
        m_speaker->playOK();
        break;
    case FAILED:
        m_speaker->playCantDo();
        break;
    case SILENT_SUCCESS:
        break;
    }
    m_indicator_light->setState(OFF);
}

void RecogniseCommandState::exitState()
{
    delete m_speech_recogniser;
    m_speech_recogniser = NULL;
    Serial.printf("[ASR] Free heap: %d\n", esp_get_free_heap_size());
}
