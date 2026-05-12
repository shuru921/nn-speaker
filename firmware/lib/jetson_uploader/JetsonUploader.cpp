#include "JetsonUploader.h"
#include <ArduinoJson.h>

const char* JetsonUploader::BOUNDARY = "----ESP32Boundary";

static const int SAMPLE_RATE   = 16000;
static const int BITS          = 16;
static const int CHANNELS      = 1;
static const int WAV_HEADER_SZ = 44;

JetsonUploader::JetsonUploader(const char* jetson_host, int port, int max_seconds, bool use_https)
    : m_jetson_host(jetson_host), m_port(port), m_use_https(use_https), m_sample_count(0)
{
    m_max_samples = SAMPLE_RATE * max_seconds;
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
    int to_copy = min(count, m_max_samples - m_sample_count);
    memcpy(m_audio_buffer + m_sample_count, samples, to_copy * sizeof(int16_t));
    m_sample_count += to_copy;
}

void JetsonUploader::writeWavHeader(Print& client, int data_bytes) {
    int byte_rate   = SAMPLE_RATE * CHANNELS * (BITS / 8);
    int block_align = CHANNELS * (BITS / 8);
    int chunk_size  = 36 + data_bytes;
    uint8_t hdr[WAV_HEADER_SZ];
    memcpy(hdr,      "RIFF", 4); memcpy(hdr+4,  &chunk_size,  4);
    memcpy(hdr+8,    "WAVE", 4);
    memcpy(hdr+12,   "fmt ", 4); int s1=16; memcpy(hdr+16, &s1, 4);
    int16_t fmt=1;  memcpy(hdr+20, &fmt, 2);
    int16_t ch=CHANNELS; memcpy(hdr+22, &ch, 2);
    memcpy(hdr+24, &SAMPLE_RATE, 4); memcpy(hdr+28, &byte_rate, 4);
    int16_t ba=block_align; memcpy(hdr+32, &ba, 2);
    int16_t bps=BITS; memcpy(hdr+34, &bps, 2);
    memcpy(hdr+36, "data", 4); memcpy(hdr+40, &data_bytes, 4);
    client.write(hdr, WAV_HEADER_SZ);
}

String JetsonUploader::sendAndGetText() {
    if (!m_audio_buffer || m_sample_count == 0) return "";

    int audio_bytes = m_sample_count * sizeof(int16_t);
    int wav_bytes   = WAV_HEADER_SZ + audio_bytes;

    String part_header =
        String("--") + BOUNDARY + "\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n"
        "Content-Type: audio/wav\r\n\r\n";
    String part_footer = String("\r\n--") + BOUNDARY + "--\r\n";
    int total_len = part_header.length() + wav_bytes + part_footer.length();

    String body = "";

    if (m_use_https) {
        // HTTPS（ngrok）
        WiFiClientSecure client;
        client.setInsecure(); // Demo 用，不驗證憑證
        client.setTimeout(15);
        if (!client.connect(m_jetson_host, m_port)) {
            Serial.printf("[Jetson] HTTPS connect failed: %s:%d\n", m_jetson_host, m_port);
            return "";
        }
        client.printf("POST /process HTTP/1.1\r\n");
        client.printf("Host: %s\r\n", m_jetson_host);
        client.printf("ngrok-skip-browser-warning: true\r\n");
        client.printf("Content-Type: multipart/form-data; boundary=%s\r\n", BOUNDARY);
        client.printf("Content-Length: %d\r\n", total_len);
        client.printf("Connection: close\r\n\r\n");
        client.print(part_header);
        writeWavHeader(client, audio_bytes);
        uint8_t* ptr = (uint8_t*)m_audio_buffer;
        for (int sent = 0; sent < audio_bytes; sent += 1024) {
            client.write(ptr + sent, min(1024, audio_bytes - sent));
            delay(1);
        }
        client.print(part_footer);
        unsigned long t = millis();
        while (!client.available() && millis() - t < 10000) delay(10);
        while (client.connected()) {
            String line = client.readStringUntil('\n');
            if (line == "\r") break;
        }
        body = client.readString();
        client.stop();
    } else {
        // HTTP（區域網路）
        WiFiClient client;
        if (!client.connect(m_jetson_host, m_port)) {
            Serial.printf("[Jetson] HTTP connect failed: %s:%d\n", m_jetson_host, m_port);
            return "";
        }
        client.printf("POST /process HTTP/1.1\r\n");
        client.printf("Host: %s:%d\r\n", m_jetson_host, m_port);
        client.printf("Content-Type: multipart/form-data; boundary=%s\r\n", BOUNDARY);
        client.printf("Content-Length: %d\r\n", total_len);
        client.printf("Connection: close\r\n\r\n");
        client.print(part_header);
        writeWavHeader(client, audio_bytes);
        uint8_t* ptr = (uint8_t*)m_audio_buffer;
        for (int sent = 0; sent < audio_bytes; sent += 1024) {
            client.write(ptr + sent, min(1024, audio_bytes - sent));
            delay(1);
        }
        client.print(part_footer);
        unsigned long t = millis();
        while (!client.available() && millis() - t < 10000) delay(10);
        while (client.connected()) {
            String line = client.readStringUntil('\n');
            if (line == "\r") break;
        }
        body = client.readString();
        client.stop();
    }

    Serial.printf("[Jetson] Response: %s\n", body.c_str());
    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, body) == DeserializationError::Ok) {
        const char* text = doc["text"];
        return text ? String(text) : "";
    }
    return "";
}
