#ifndef _tts_player_h_
#define _tts_player_h_

#include <Arduino.h>

class I2SOutput;

class TTSPlayer
{
public:
    TTSPlayer(I2SOutput *i2s_output);

    // Fetch TTS audio from OpenAI and play it.
    // Blocks until playback is complete (or times out).
    // API key is read from NVS Credentials automatically.
    void speak(const String &text);

private:
    I2SOutput *m_output;

    // Downsample 24kHz mono PCM → 16kHz (3:2 ratio, linear interpolation).
    // Returns the number of output samples written to `out`.
    static int downsample24to16(const int16_t *in, int in_count, int16_t *out);
};

#endif
