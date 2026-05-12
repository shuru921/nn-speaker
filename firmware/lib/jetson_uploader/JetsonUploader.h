#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

// 收集音訊樣本並以 WAV 格式 POST 到 Jetson /process
// 支援 HTTP（區域網路）和 HTTPS（ngrok）
class JetsonUploader {
public:
    // use_https=false → 區域網路 HTTP
    // use_https=true  → ngrok HTTPS（port 443）
    JetsonUploader(const char* jetson_host, int port, int max_seconds = 10, bool use_https = false);
    ~JetsonUploader();

    void addSamples(const int16_t* samples, int count);
    String sendAndGetText();

private:
    const char* m_jetson_host;
    int m_port;
    bool m_use_https;
    int16_t* m_audio_buffer;
    int m_sample_count;
    int m_max_samples;

    void writeWavHeader(Print& client, int data_bytes);
    String doRequest(Print& writer, Client& reader, int total_len,
                     const String& part_header, const String& part_footer, int audio_bytes);
    static const char* BOUNDARY;
};
