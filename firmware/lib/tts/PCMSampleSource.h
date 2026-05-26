#ifndef _pcm_sample_source_h_
#define _pcm_sample_source_h_

#include "SampleSource.h"
#include <esp_heap_caps.h>

// SampleSource wrapping a raw 16kHz mono PCM buffer stored in PSRAM.
// Ownership of `buf` is transferred in; destructor frees it.
class PCMSampleSource : public SampleSource
{
public:
    PCMSampleSource(int16_t *buf, int num_samples)
        : m_buf(buf), m_total(num_samples), m_pos(0) {}

    ~PCMSampleSource()
    {
        if (m_buf)
            heap_caps_free(m_buf);
    }

    int getFrames(Frame_t *frames, int n) override
    {
        int count = 0;
        while (count < n && m_pos < m_total)
        {
            int16_t s = m_buf[m_pos++];
            frames[count].left = s;
            frames[count].right = s;
            count++;
        }
        return count;
    }

    bool available() override { return m_pos < m_total; }

    // Safe to poll from a different task (volatile read)
    bool isComplete() const { return m_pos >= m_total; }

private:
    int16_t *m_buf;
    int m_total;
    volatile int m_pos;
};

#endif
