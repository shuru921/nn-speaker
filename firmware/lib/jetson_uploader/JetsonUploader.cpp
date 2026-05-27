#include "JetsonUploader.h"

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

void JetsonUploader::buildWavHeader(uint8_t hdr[WAV_HEADER_SZ], int data_bytes) {
    int byte_rate   = SAMPLE_RATE * CHANNELS * (BITS / 8);
    int block_align = CHANNELS * (BITS / 8);
    int chunk_size  = 36 + data_bytes;
    memcpy(hdr,    "RIFF", 4); memcpy(hdr+4,  &chunk_size,  4);
    memcpy(hdr+8,  "WAVE", 4);
    memcpy(hdr+12, "fmt ", 4); int s1=16; memcpy(hdr+16, &s1, 4);
    int16_t fmt=1; memcpy(hdr+20, &fmt, 2);
    int16_t ch=CHANNELS; memcpy(hdr+22, &ch, 2);
    memcpy(hdr+24, &SAMPLE_RATE, 4); memcpy(hdr+28, &byte_rate, 4);
    int16_t ba=block_align; memcpy(hdr+32, &ba, 2);
    int16_t bps=BITS; memcpy(hdr+34, &bps, 2);
    memcpy(hdr+36, "data", 4); memcpy(hdr+40, &data_bytes, 4);
}

void JetsonUploader::sendAudio() {
    if (!m_audio_buffer || m_sample_count == 0) return;

    int audio_bytes = m_sample_count * sizeof(int16_t);
    int wav_bytes   = WAV_HEADER_SZ + audio_bytes;

    String part_header =
        String("--") + BOUNDARY + "\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n"
        "Content-Type: audio/wav\r\n\r\n";
    String part_footer = String("\r\n--") + BOUNDARY + "--\r\n";
    int total_len = part_header.length() + wav_bytes + part_footer.length();

    uint8_t wav_hdr[WAV_HEADER_SZ];
    buildWavHeader(wav_hdr, audio_bytes);

    auto writeAll = [](Client& c, const uint8_t* data, size_t len) {
        size_t written = 0;
        while (written < len) {
            size_t n = c.write(data + written, len - written);
            if (n == 0) {
                return false;
            }
            written += n;
        }
        return true;
    };

    auto sendOverClient = [&](Client& client) {
        client.printf("POST /process HTTP/1.1\r\n");
        client.printf("Host: %s\r\n", m_jetson_host);
        client.printf("ngrok-skip-browser-warning: true\r\n");
        client.printf("Content-Type: multipart/form-data; boundary=%s\r\n", BOUNDARY);
        client.printf("Content-Length: %d\r\n", total_len);
        client.printf("Connection: close\r\n\r\n");

        if (!writeAll(client, (uint8_t*)part_header.c_str(), part_header.length()) ||
            !writeAll(client, wav_hdr, WAV_HEADER_SZ)) {
            Serial.println("[Jetson] upload failed while sending WAV header");
            client.stop();
            return;
        }

        uint8_t* ptr = (uint8_t*)m_audio_buffer;
        for (int sent = 0; sent < audio_bytes; sent += 4096) {
            int chunk = min(4096, audio_bytes - sent);
            if (!writeAll(client, ptr + sent, chunk)) {
                Serial.printf("[Jetson] upload failed after %d/%d audio bytes\n", sent, audio_bytes);
                client.stop();
                return;
            }
        }

        if (!writeAll(client, (uint8_t*)part_footer.c_str(), part_footer.length())) {
            Serial.println("[Jetson] upload failed while sending multipart footer");
            client.stop();
            return;
        }
        client.flush();

        String response;
        unsigned long deadline = millis() + 10000;
        while ((client.connected() || client.available()) && millis() < deadline) {
            while (client.available() && response.length() < 512) {
                response += (char)client.read();
            }
            if (response.length() >= 512) {
                break;
            }
            delay(10);
        }

        client.stop();
        Serial.printf("[Jetson] audio sent (%d bytes WAV)\n", wav_bytes);
        if (response.length() > 0) {
            Serial.printf("[Jetson] response:\n%s\n", response.c_str());
        } else {
            Serial.println("[Jetson] no HTTP response before timeout");
        }
    };

    if (m_use_https) {
        WiFiClientSecure client;
        client.setInsecure();
        client.setTimeout(15);
        if (!client.connect(m_jetson_host, m_port)) {
            Serial.printf("[Jetson] HTTPS connect failed: %s:%d\n", m_jetson_host, m_port);
            return;
        }
        sendOverClient(client);
    } else {
        WiFiClient client;
        if (!client.connect(m_jetson_host, m_port)) {
            Serial.printf("[Jetson] HTTP connect failed: %s:%d\n", m_jetson_host, m_port);
            return;
        }
        sendOverClient(client);
    }
}
