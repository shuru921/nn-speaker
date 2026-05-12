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

#define WINDOW_SIZE 320
#define STEP_SIZE 160
#define POOLING_SIZE 6
#define AUDIO_LENGTH 16000

RecogniseCommandState::RecogniseCommandState(I2SSampler *sample_provider, IndicatorLight *indicator_light, Speaker *speaker, IntentProcessor *intent_processor)
{
    // save the sample provider for use later
    m_sample_provider = sample_provider;
    m_indicator_light = indicator_light;
    m_speaker = speaker;
    m_intent_processor = intent_processor;
    m_speech_recogniser = NULL;
}
void RecogniseCommandState::enterState()
{
    // indicate that we are now recording audio
    m_indicator_light->setState(ON);
    m_speaker->playReady();

    // stash the start time - we will limit ourselves to 5 seconds of data
    m_start_time = millis();
    m_elapsed_time = 0;
    m_last_audio_position = -1;

    uint32_t free_ram = esp_get_free_heap_size();
    Serial.printf("Free ram before connection %d\n", free_ram);

    m_speech_recogniser = new JetsonUploader(JETSON_IP, JETSON_PORT, 3);

    Serial.println("Ready for action");

    free_ram = esp_get_free_heap_size();
    Serial.printf("Free ram after connection %d\n", free_ram);
}
bool RecogniseCommandState::run()
{
    if (!m_speech_recogniser)
    {
        Serial.println("Error - No speech recogniser");
        return true;
    }
    if (m_last_audio_position == -1)
    {
        // set to 1 seconds in the past the allow for the really slow connection time
        m_last_audio_position = m_sample_provider->getCurrentWritePosition() - 16000;
    }
    // how many samples have been captured since we last ran
    int audio_position = m_sample_provider->getCurrentWritePosition();
    // work out how many samples there are taking into account that we can wrap around
    int sample_count = (audio_position - m_last_audio_position + m_sample_provider->getRingBufferSize()) % m_sample_provider->getRingBufferSize();
    // Serial.printf("Last sample position %d, current position %d, number samples %d\n", m_last_audio_position, audio_position, sample_count);
    if (sample_count > 0)
    {
        // 收集音訊樣本到 JetsonUploader buffer
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
        delete (reader);

        // has 3 seconds passed?
        unsigned long current_time = millis();
        m_elapsed_time += current_time - m_start_time;
        m_start_time = current_time;
        if (m_elapsed_time > 3000)
        {
            m_indicator_light->setState(PULSING);
            Serial.println("3 seconds elapsed - sending to Jetson ASR");
            String text = m_speech_recogniser->sendAndGetText();
            Serial.printf("Jetson ASR result: %s\n", text.c_str());

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
            return true;
        }
    }
    // still work to do, stay in this state
    return false;
}

void RecogniseCommandState::exitState()
{
    // clean up the speech recognizer client as it takes up a lot of RAM
    delete m_speech_recogniser;
    m_speech_recogniser = NULL;
    uint32_t free_ram = esp_get_free_heap_size();
    Serial.printf("Free ram after request %d\n", free_ram);
}