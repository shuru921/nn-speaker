#ifndef _whisper_uploader_h_
#define _whisper_uploader_h_

#include <stdint.h>
#include <Arduino.h>

class WhisperUploader
{
public:
    // max_samples: maximum number of int16 samples to buffer (e.g. 5*16000 = 5 sec)
    WhisperUploader(const char *api_key, int max_samples = 5 * 16000);
    ~WhisperUploader();

    // Copy samples from your ring buffer into internal PSRAM buffer
    void addSamples(const int16_t *samples, int count);
    int getSampleCount() { return m_sample_count; }

    // Send buffered audio to Whisper API and return the recognised text
    String getTranscription();

private:
    const char *m_api_key;
    int16_t *m_audio_buffer;
    int m_sample_count;
    int m_max_samples;

    void writeWavHeader(uint8_t *buf, int num_samples);
};

#endif
