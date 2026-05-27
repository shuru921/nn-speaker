#include <Arduino.h>
#include <esp_heap_caps.h>
#include "I2SSampler.h"
#include "RingBuffer.h"
#include "RecogniseCommandState.h"
#include "IndicatorLight.h"
#include "Speaker.h"
#include "JetsonUploader.h"
#include "../config.h"

#define MAX_RECORD_SECONDS 10
#define RECORD_DURATION_MS 5000

RecogniseCommandState::RecogniseCommandState(I2SSampler *sample_provider, IndicatorLight *indicator_light, Speaker *speaker)
{
    m_sample_provider   = sample_provider;
    m_indicator_light   = indicator_light;
    m_speaker           = speaker;
    m_speech_recogniser = NULL;
}

void RecogniseCommandState::enterState()
{
    m_indicator_light->setState(ON);
    m_speaker->playReady();
    m_last_audio_position = -1;
    m_start_time   = millis();
    m_elapsed_time = 0;
    m_speech_recogniser = new JetsonUploader(JETSON_HOST, JETSON_PORT, MAX_RECORD_SECONDS, JETSON_USE_HTTPS);
    Serial.println("[ASR] Recording...");
}

bool RecogniseCommandState::run()
{
    if (!m_speech_recogniser) return true;

    if (m_last_audio_position == -1)
        m_last_audio_position = m_sample_provider->getCurrentWritePosition() - 16000;

    int audio_position = m_sample_provider->getCurrentWritePosition();
    int sample_count   = (audio_position - m_last_audio_position
                          + m_sample_provider->getRingBufferSize())
                         % m_sample_provider->getRingBufferSize();

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

        unsigned long now = millis();
        m_elapsed_time += now - m_start_time;
        m_start_time = now;

        if (m_elapsed_time > RECORD_DURATION_MS)
        {
            finish();
            return true;
        }
    }
    return false;
}

void RecogniseCommandState::finish()
{
    if (!m_speech_recogniser) return;

    m_indicator_light->setState(PULSING);
    m_sample_provider->stop();
    m_speaker->stopOutput();
    Serial.printf("[ASR] Internal heap before HTTPS: free=%u largest=%u\n",
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    m_speech_recogniser->sendAudio();   // 送出 WAV，不等回應
    Serial.printf("[ASR] Internal heap after HTTPS: free=%u largest=%u\n",
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    m_speaker->resumeOutput();
    m_sample_provider->resume();
    m_speaker->playOK();
    m_indicator_light->setState(OFF);

    // 釋放 uploader，exitState() 不需要再 delete
    delete m_speech_recogniser;
    m_speech_recogniser = NULL;
}

void RecogniseCommandState::exitState()
{
    // m_speech_recogniser 已在 finish() 轉給 task，通常為 NULL
    if (m_speech_recogniser)
    {
        delete m_speech_recogniser;
        m_speech_recogniser = NULL;
    }
    Serial.printf("[ASR] Free heap: %d\n", esp_get_free_heap_size());
}
