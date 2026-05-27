#ifndef __i2s_output_h__
#define __i2s_output_h__

#include <Arduino.h>
#include "driver/i2s.h"

class SampleSource;

/**
 * Base Class for both the ADC and I2S sampler
 **/
class I2SOutput
{
private:
    // I2S write task
    TaskHandle_t m_i2sWriterTaskHandle;
    // i2s writer queue
    QueueHandle_t m_i2sQueue;
    // i2s port
    i2s_port_t m_i2sPort;
    i2s_pin_config_t m_i2sPins;
    i2s_config_t m_i2sConfig;
    bool m_started;
    // src of samples for us to play
    SampleSource *m_sample_generator;

public:
    I2SOutput()
    {
        m_i2sWriterTaskHandle = NULL;
        m_i2sQueue = NULL;
        m_started = false;
        m_sample_generator = NULL;
    }
    void start(i2s_port_t i2sPort, i2s_pin_config_t &i2sPins, i2s_config_t i2sConfig);
    void stop();
    void resume();
    void setSampleGenerator(SampleSource *sample_generator);
    friend void i2sWriterTask(void *param);
};

#endif
