#include "JetsonUploader.h"
#include <ArduinoJson.h>

const char* JetsonUploader::BOUNDARY = "----ESP32Boundary";

// WAV 格式常數
static const int SAMPLE_RATE   = 16000;
static const int BITS          = 16;
static const int CHANNELS      = 1;
static const int WAV_HEADER_SZ = 44;

JetsonUploader::JetsonUploader(const char* jetson_ip, int port, int max_seconds)
    : m_jetson_ip(jetson_ip), m_port(port), m_sample_count(0)
{
    m_max_samples = SAMPLE_RATE * max_seconds;
    // 優先配置到 PSRAM，內部 RAM 不夠放 3 秒音訊
    m_audio_buffer = (int16_t*)heap_caps_malloc(
        m_max_samples * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!m_audio_buffer) {
        Serial.println("[Jetson] PSRAM malloc failed, fallback to internal");
        m_audio_buffer = (int16_t*)malloc(m_max_samples * sizeof(int16_t));
    }
}

JetsonUploader::~JetsonUploader() {
    free(m_audio_buffer);
}

void JetsonUploader::addSamples(const int16_t* samples, int count) {
    if (!m_audio_buffer) return;
    int available = m_max_samples - m_sample_count;
    int to_copy   = min(count, available);
    memcpy(m_audio_buffer + m_sample_count, samples, to_copy * sizeof(int16_t));
    m_sample_count += to_copy;
}

// 寫 44-byte WAV header 到 WiFiClient
void JetsonUploader::writeWavHeader(WiFiClient& client, int data_bytes) {
    int byte_rate    = SAMPLE_RATE * CHANNELS * (BITS / 8);
    int block_align  = CHANNELS * (BITS / 8);
    int chunk_size   = 36 + data_bytes;

    uint8_t hdr[WAV_HEADER_SZ];
    // RIFF chunk
    memcpy(hdr,      "RIFF", 4);
    memcpy(hdr + 4,  &chunk_size,  4);
    memcpy(hdr + 8,  "WAVE", 4);
    // fmt sub-chunk
    memcpy(hdr + 12, "fmt ", 4);
    int sub1 = 16; memcpy(hdr + 16, &sub1,        4);
    int16_t fmt = 1; memcpy(hdr + 20, &fmt,        2); // PCM
    int16_t ch  = CHANNELS; memcpy(hdr + 22, &ch,  2);
    memcpy(hdr + 24, &SAMPLE_RATE,  4);
    memcpy(hdr + 28, &byte_rate,    4);
    int16_t ba = block_align; memcpy(hdr + 32, &ba, 2);
    int16_t bps = BITS;       memcpy(hdr + 34, &bps,2);
    // data sub-chunk
    memcpy(hdr + 36, "data", 4);
    memcpy(hdr + 40, &data_bytes,   4);

    client.write(hdr, WAV_HEADER_SZ);
}

String JetsonUploader::sendAndGetText() {
    if (!m_audio_buffer || m_sample_count == 0) return "";

    WiFiClient client;
    if (!client.connect(m_jetson_ip, m_port)) {
        Serial.printf("[Jetson] Cannot connect to %s:%d\n", m_jetson_ip, m_port);
        return "";
    }

    int audio_bytes = m_sample_count * sizeof(int16_t);
    int wav_bytes   = WAV_HEADER_SZ + audio_bytes;

    // 組 multipart body
    String part_header =
        String("--") + BOUNDARY + "\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n"
        "Content-Type: audio/wav\r\n\r\n";
    String part_footer = String("\r\n--") + BOUNDARY + "--\r\n";

    int total_len = part_header.length() + wav_bytes + part_footer.length();

    // HTTP request headers
    client.printf("POST /process HTTP/1.1\r\n");
    client.printf("Host: %s:%d\r\n", m_jetson_ip, m_port);
    client.printf("Content-Type: multipart/form-data; boundary=%s\r\n", BOUNDARY);
    client.printf("Content-Length: %d\r\n", total_len);
    client.printf("Connection: close\r\n\r\n");

    // body
    client.print(part_header);
    writeWavHeader(client, audio_bytes);
    // 分塊送音訊避免 watchdog timeout
    const int CHUNK = 1024;
    uint8_t* ptr = (uint8_t*)m_audio_buffer;
    for (int sent = 0; sent < audio_bytes; sent += CHUNK) {
        int sz = min(CHUNK, audio_bytes - sent);
        client.write(ptr + sent, sz);
        delay(1);
    }
    client.print(part_footer);

    // 等待並讀回 response
    unsigned long t = millis();
    while (!client.available() && millis() - t < 10000) delay(10);

    // 跳過 HTTP headers
    while (client.connected()) {
        String line = client.readStringUntil('\n');
        if (line == "\r") break;
    }

    String body = client.readString();
    client.stop();

    Serial.printf("[Jetson] Response: %s\n", body.c_str());

    // 解析 JSON 取出 text
    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, body) == DeserializationError::Ok) {
        const char* text = doc["text"];
        return text ? String(text) : "";
    }
    return "";
}
