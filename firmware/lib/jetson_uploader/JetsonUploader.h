#pragma once
#include <Arduino.h>
#include <WiFi.h>

// 收集音訊樣本並以 WAV 格式 POST 到 Jetson /process
class JetsonUploader {
public:
    JetsonUploader(const char* jetson_ip, int port, int max_seconds = 3);
    ~JetsonUploader();

    // 加入麥克風樣本（16-bit PCM, 16kHz）
    void addSamples(const int16_t* samples, int count);

    // 錄音結束後傳送，回傳辨識文字（失敗回傳空字串）
    String sendAndGetText();

private:
    const char* m_jetson_ip;
    int m_port;
    int16_t* m_audio_buffer;   // 存在 PSRAM
    int m_sample_count;
    int m_max_samples;

    void writeWavHeader(WiFiClient& client, int data_bytes);
    int buildMultipartHeader(char* buf, int audio_bytes);
    static const char* BOUNDARY;
};
