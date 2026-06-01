#ifndef __wav_file_reader_h__
#define __wav_file_reader_h__

#include <SPIFFS.h>
#include <FS.h>
#include "SampleSource.h"

class WAVFileReader : public SampleSource
{
private:
    int m_num_channels;
    bool m_repeat;
    bool m_valid;
    File m_file;

public:
    WAVFileReader(const char *file_name, bool repeat = false);
    ~WAVFileReader();
    int getFrames(Frame_t *frames, int number_frames);
    bool available();
    bool isValid() { return m_valid; }
    void reset();
};

#endif