#include "TTSPlayer.h"
#include "PCMSampleSource.h"
#include "I2SOutput.h"
#include "Credentials.h"
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <algorithm>

// Max PCM buffer for 24kHz raw input: ~12 seconds of speech
static const int MAX_24K_BYTES = 576000; // 12s × 24000Hz × 2 bytes

TTSPlayer::TTSPlayer(I2SOutput *output) : m_output(output) {}

// 24kHz → 16kHz (3:2 ratio).
// For every 3 input samples, produce 2 output samples via linear interpolation:
//   out[0] = in[0]
//   out[1] = (in[1] + in[2]) / 2
int TTSPlayer::downsample24to16(const int16_t *in, int in_count, int16_t *out)
{
    int out_count = 0;
    for (int i = 0; i + 2 < in_count; i += 3)
    {
        out[out_count++] = in[i];
        out[out_count++] = (int16_t)(((int32_t)in[i + 1] + in[i + 2]) / 2);
    }
    return out_count;
}

void TTSPlayer::speak(const String &text)
{
    String api_key = Credentials::getOpenAIKey();
    if (api_key.isEmpty())
    {
        Serial.println("TTSPlayer: no API key in NVS");
        return;
    }

    Serial.println("TTSPlayer: connecting to api.openai.com...");
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(30);

    if (!client.connect("api.openai.com", 443))
    {
        Serial.println("TTSPlayer: TLS connection failed");
        return;
    }

    // Build JSON request body
    StaticJsonDocument<512> doc;
    doc["model"] = "tts-1";
    doc["input"] = text;
    doc["voice"] = "alloy";
    doc["response_format"] = "pcm"; // raw 16-bit signed 24kHz mono
    String body;
    serializeJson(doc, body);

    client.println("POST /v1/audio/speech HTTP/1.1");
    client.println("Host: api.openai.com");
    client.printf("Authorization: Bearer %s\r\n", api_key.c_str());
    client.println("Content-Type: application/json");
    client.printf("Content-Length: %d\r\n", body.length());
    client.println("Connection: close");
    client.println();
    client.print(body);

    // Parse response headers
    int http_status = -1;
    int content_length = -1;
    bool is_chunked = false;
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
                sscanf(line, "HTTP/1.1 %d", &http_status);
            else if (strncmp(line, "Content-Length:", 15) == 0)
                sscanf(line, "Content-Length: %d", &content_length);
            else if (strstr(line, "chunked"))
                is_chunked = true;
        }
    }
    Serial.printf("TTSPlayer: HTTP %d, len=%d, chunked=%d\n", http_status, content_length, is_chunked);

    if (http_status != 200)
    {
        Serial.println("TTSPlayer: bad HTTP status, aborting");
        return;
    }

    // Allocate raw 24kHz PCM buffer in PSRAM
    uint8_t *raw = (uint8_t *)heap_caps_malloc(MAX_24K_BYTES, MALLOC_CAP_SPIRAM);
    if (!raw)
    {
        Serial.println("TTSPlayer: PSRAM alloc failed");
        return;
    }

    int raw_bytes = 0;

    if (content_length > 0)
    {
        int remaining = std::min(content_length, MAX_24K_BYTES);
        while (remaining > 0 && client.connected())
        {
            int n = client.read(raw + raw_bytes, remaining);
            if (n > 0) { raw_bytes += n; remaining -= n; }
            else delay(1);
        }
    }
    else if (is_chunked)
    {
        while (client.connected())
        {
            char sz[16];
            int len = client.readBytesUntil('\n', sz, sizeof(sz) - 1);
            if (len <= 0) { delay(1); continue; }
            sz[len] = '\0';
            int chunk = strtol(sz, nullptr, 16);
            if (chunk == 0) break;
            int remaining = std::min(chunk, MAX_24K_BYTES - raw_bytes);
            while (remaining > 0)
            {
                int n = client.read(raw + raw_bytes, remaining);
                if (n > 0) { raw_bytes += n; remaining -= n; }
                else delay(1);
            }
            client.readStringUntil('\n'); // consume trailing \r\n
            if (raw_bytes >= MAX_24K_BYTES) break;
        }
    }
    else
    {
        unsigned long last_rx = millis();
        while ((client.connected() || client.available()) && raw_bytes < MAX_24K_BYTES)
        {
            while (client.available() && raw_bytes < MAX_24K_BYTES)
            {
                raw[raw_bytes++] = client.read();
                last_rx = millis();
            }
            if (millis() - last_rx > 3000) break;
            delay(1);
        }
    }

    Serial.printf("TTSPlayer: received %d bytes of 24kHz PCM\n", raw_bytes);

    // Downsample 24kHz → 16kHz
    int in_samples = raw_bytes / 2;
    int max_out = (in_samples * 2) / 3 + 1;
    int16_t *pcm16 = (int16_t *)heap_caps_malloc(max_out * 2, MALLOC_CAP_SPIRAM);
    if (!pcm16)
    {
        Serial.println("TTSPlayer: PSRAM alloc for 16kHz failed");
        heap_caps_free(raw);
        return;
    }
    int out_samples = downsample24to16((const int16_t *)raw, in_samples, pcm16);
    heap_caps_free(raw);

    Serial.printf("TTSPlayer: playing %d samples (~%.1f sec)\n",
                  out_samples, out_samples / 16000.0f);

    // Hand off to I2SOutput and wait for completion
    PCMSampleSource *src = new PCMSampleSource(pcm16, out_samples);
    m_output->setSampleGenerator(src);

    // Wait until i2sWriterTask has consumed all frames
    unsigned long play_start = millis();
    unsigned long play_timeout = (unsigned long)(out_samples / 16000.0f * 1000) + 3000;
    while (!src->isComplete() && millis() - play_start < play_timeout)
    {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    vTaskDelay(pdMS_TO_TICKS(300)); // let DMA drain last buffer

    // Disconnect src from writer task BEFORE deleting it
    m_output->setSampleGenerator(nullptr);
    vTaskDelay(pdMS_TO_TICKS(50)); // let writer task see nullptr
    delete src; // also frees pcm16 via PCMSampleSource destructor
}
