#include "WhisperUploader.h"
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <algorithm>

static const char *BOUNDARY = "----FormBoundary7MA4YWxkTrZu0gW";

WhisperUploader::WhisperUploader(const char *api_key, int max_samples)
    : m_api_key(api_key), m_sample_count(0), m_max_samples(max_samples)
{
    // Allocate audio buffer in PSRAM to avoid eating internal heap
    m_audio_buffer = (int16_t *)heap_caps_malloc(max_samples * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!m_audio_buffer)
    {
        Serial.println("WhisperUploader: failed to alloc PSRAM audio buffer");
    }
}

WhisperUploader::~WhisperUploader()
{
    if (m_audio_buffer)
    {
        heap_caps_free(m_audio_buffer);
    }
}

void WhisperUploader::addSamples(const int16_t *samples, int count)
{
    if (!m_audio_buffer)
        return;
    int to_add = std::min(count, m_max_samples - m_sample_count);
    memcpy(m_audio_buffer + m_sample_count, samples, to_add * sizeof(int16_t));
    m_sample_count += to_add;
}

// Writes a standard 44-byte PCM WAV header into buf
void WhisperUploader::writeWavHeader(uint8_t *buf, int num_samples)
{
    uint32_t data_size = num_samples * 2;
    uint32_t file_size = data_size + 36;
    uint32_t sample_rate = 16000;
    uint32_t byte_rate = 32000; // 16000 * 1ch * 2 bytes
    uint16_t block_align = 2;
    uint16_t bits = 16;
    uint16_t channels = 1;
    uint16_t audio_format = 1; // PCM

    memcpy(buf + 0, "RIFF", 4);
    memcpy(buf + 4, &file_size, 4);
    memcpy(buf + 8, "WAVE", 4);
    memcpy(buf + 12, "fmt ", 4);
    uint32_t fmt_chunk = 16;
    memcpy(buf + 16, &fmt_chunk, 4);
    memcpy(buf + 20, &audio_format, 2);
    memcpy(buf + 22, &channels, 2);
    memcpy(buf + 24, &sample_rate, 4);
    memcpy(buf + 28, &byte_rate, 4);
    memcpy(buf + 32, &block_align, 2);
    memcpy(buf + 34, &bits, 2);
    memcpy(buf + 36, "data", 4);
    memcpy(buf + 40, &data_size, 4);
}

String WhisperUploader::getTranscription()
{
    if (!m_audio_buffer || m_sample_count == 0)
    {
        Serial.println("WhisperUploader: no audio data");
        return "";
    }

    // Build the multipart body parts as strings
    String part_model =
        String("--") + BOUNDARY + "\r\n"
        "Content-Disposition: form-data; name=\"model\"\r\n"
        "\r\n"
        "whisper-1\r\n";

    String part_language =
        String("--") + BOUNDARY + "\r\n"
        "Content-Disposition: form-data; name=\"language\"\r\n"
        "\r\n"
        "zh\r\n";

    String part_file_header =
        String("--") + BOUNDARY + "\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n"
        "Content-Type: audio/wav\r\n"
        "\r\n";

    String part_file_tail = "\r\n";
    String end_boundary = String("--") + BOUNDARY + "--\r\n";

    int wav_data_size = 44 + m_sample_count * 2; // WAV header + PCM
    int content_length = part_model.length()
                       + part_language.length()
                       + part_file_header.length()
                       + wav_data_size
                       + part_file_tail.length()
                       + end_boundary.length();

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(30);

    Serial.println("WhisperUploader: connecting to api.openai.com...");
    if (!client.connect("api.openai.com", 443))
    {
        Serial.println("WhisperUploader: TLS connection failed");
        return "";
    }

    // HTTP request headers
    client.println("POST /v1/audio/transcriptions HTTP/1.1");
    client.println("Host: api.openai.com");
    client.printf("Authorization: Bearer %s\r\n", m_api_key);
    client.printf("Content-Type: multipart/form-data; boundary=%s\r\n", BOUNDARY);
    client.printf("Content-Length: %d\r\n", content_length);
    client.println("Connection: close");
    client.println();

    // Send multipart body
    client.print(part_model);
    client.print(part_language);
    client.print(part_file_header);

    // WAV header (44 bytes)
    uint8_t wav_header[44];
    writeWavHeader(wav_header, m_sample_count);
    client.write(wav_header, 44);

    // PCM audio data in 1 KB chunks
    const uint8_t *pcm = (const uint8_t *)m_audio_buffer;
    int remaining = m_sample_count * 2;
    while (remaining > 0)
    {
        int batch = std::min(remaining, 1024);
        client.write(pcm, batch);
        pcm += batch;
        remaining -= batch;
    }

    client.print(part_file_tail);
    client.print(end_boundary);

    // Parse HTTP status line
    int status = -1;
    while (client.connected())
    {
        char line[256];
        int len = client.readBytesUntil('\n', line, sizeof(line) - 1);
        if (len > 0)
        {
            line[len] = '\0';
            if (line[0] == '\r')
                break; // end of headers
            if (strncmp(line, "HTTP", 4) == 0)
                sscanf(line, "HTTP/1.1 %d", &status);
        }
    }
    Serial.printf("WhisperUploader: HTTP status %d\n", status);

    // Read response body
    String body;
    unsigned long last_rx = millis();
    while (client.connected() || client.available())
    {
        while (client.available())
        {
            body += (char)client.read();
            last_rx = millis();
        }
        if (millis() - last_rx > 5000)
            break;
        delay(1);
    }
    Serial.println("Whisper response: " + body);

    if (status == 200)
    {
        StaticJsonDocument<256> doc;
        if (!deserializeJson(doc, body))
        {
            const char *text = doc["text"];
            return text ? String(text) : "";
        }
    }
    return "";
}
