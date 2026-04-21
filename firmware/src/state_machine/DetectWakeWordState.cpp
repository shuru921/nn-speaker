#include <Arduino.h>
#include "I2SSampler.h"
#include "AudioProcessor.h"
#include "NeuralNetwork.h"
#include "RingBuffer.h"
#include "DetectWakeWordState.h"
#include "../IndicatorLight.h"

#define WINDOW_SIZE 320
#define STEP_SIZE 160
#define POOLING_SIZE 6
#define AUDIO_LENGTH 16000

DetectWakeWordState::DetectWakeWordState(I2SSampler *sample_provider, IndicatorLight *indicator_light)
{
    // save the sample provider for use later
    m_sample_provider = sample_provider;
    m_indicator_light = indicator_light;
    // some stats on performance
    m_average_detect_time = 0;
    m_number_of_runs = 0;
    m_nn = NULL;
    m_audio_processor = NULL;
}
void DetectWakeWordState::enterState()
{
    m_indicator_light->setState(OFF);
    // create our audio processor
    m_audio_processor = new AudioProcessor(AUDIO_LENGTH, WINDOW_SIZE, STEP_SIZE, POOLING_SIZE);
    Serial.println("Created audio processor");

    // recreate the neural network only while this state is active
    // so the tensor arena can be released before HTTPS/TLS is started
    m_nn = new NeuralNetwork();
    Serial.println("Created Neural Net");

    m_number_of_detections = 0;
}
bool DetectWakeWordState::run()
{
    if (!m_nn || !m_audio_processor)
    {
        return false;
    }

    // time how long this takes for stats
    long start = millis();
    // get access to the samples that have been read in
    RingBufferAccessor *reader = m_sample_provider->getRingBufferReader();
    // rewind by 1 second
    reader->rewind(16000);
    // get hold of the input buffer for the neural network so we can feed it data
    float *input_buffer = m_nn->getInputBuffer();
    // process the samples to get the spectrogram
    m_audio_processor->get_spectrogram(reader, input_buffer);
    // finished with the sample reader
    delete reader;
    // get the prediction for the spectrogram
    float output = m_nn->predict();
    long end = millis();
    // compute the stats
    m_average_detect_time = (end - start) * 0.1 + m_average_detect_time * 0.9;
    m_number_of_runs++;
    // log out some timing info
    if (m_number_of_runs == 100)
    {
        m_number_of_runs = 0;
        Serial.printf("Average detection time %.fms\n", m_average_detect_time);
    }
    // log every output to debug false positives
    Serial.printf("output=%.4f\n", output);
    // use quite a high threshold to prevent false positives
    if (output > 0.97)
    {
        m_number_of_detections++;
        if (m_number_of_detections > 1)
        {
            m_number_of_detections = 0;
            uint32_t free_ram = esp_get_free_heap_size();
            Serial.printf("Free ram after DetectWakeWord cleanup %d\n", free_ram);

            // detected the wake word in several runs, move to the next state
            Serial.printf("P(%.2f): Here I am, brain the size of a planet...\n", output);
            // light up LED immediately on wake word detection
            m_indicator_light->setState(ON);
            return true;
        }
    }
    // nothing detected stay in the current state
    return false;
}
void DetectWakeWordState::exitState()
{
    // Release wake-word detection resources to maximize free heap for TLS.
    uint32_t free_ram = esp_get_free_heap_size();
    Serial.printf("Free ram before DetectWakeWord cleanup %d\n", free_ram);

    delete m_audio_processor;
    m_audio_processor = NULL;

    delete m_nn;
    m_nn = NULL;

    free_ram = esp_get_free_heap_size();
    Serial.printf("Free ram after DetectWakeWord cleanup %d\n", free_ram);
}
