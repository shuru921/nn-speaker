#include "JetsonWSClient.h"
#include <ArduinoJson.h>

JetsonWSClient::JetsonWSClient(const char *host, uint16_t port)
    : m_host(host), m_port(port), m_connected(false), m_has_response(false)
{
}

void JetsonWSClient::connect()
{
    // Register event callback using lambda (C++11, supported on ESP32)
    m_ws.onEvent([this](WStype_t type, uint8_t *payload, size_t length) {
        this->onEvent(type, payload, length);
    });
    // Port 443 → WSS (WebSocket Secure, used by ngrok)
    // Other ports → plain WS (used on local LAN)
    if (m_port == 443)
    {
        m_ws.beginSSL(m_host, m_port, "/");
        Serial.printf("JetsonWSClient: connecting to wss://%s:%d\n", m_host, m_port);
    }
    else
    {
        m_ws.begin(m_host, m_port, "/");
        Serial.printf("JetsonWSClient: connecting to ws://%s:%d\n", m_host, m_port);
    }
    m_ws.setReconnectInterval(3000);
}

void JetsonWSClient::loop()
{
    m_ws.loop();
}

void JetsonWSClient::sendCommand(const String &text)
{
    if (!m_connected)
    {
        Serial.println("JetsonWSClient: not connected, cannot send");
        return;
    }
    StaticJsonDocument<256> doc;
    doc["type"] = "command";
    doc["text"] = text;
    String msg;
    serializeJson(doc, msg);
    m_ws.sendTXT(msg);
    Serial.printf("JetsonWSClient: sent → %s\n", msg.c_str());
}

String JetsonWSClient::takeResponse()
{
    m_has_response = false;
    return m_response;
}

void JetsonWSClient::onEvent(WStype_t type, uint8_t *payload, size_t length)
{
    switch (type)
    {
    case WStype_CONNECTED:
        m_connected = true;
        Serial.println("JetsonWSClient: connected");
        break;

    case WStype_DISCONNECTED:
        m_connected = false;
        Serial.println("JetsonWSClient: disconnected, will retry...");
        break;

    case WStype_TEXT:
    {
        // Expect: {"type":"response","text":"..."}
        StaticJsonDocument<256> doc;
        DeserializationError err = deserializeJson(doc, payload, length);
        if (!err && doc["type"] == "response")
        {
            m_response = doc["text"].as<String>();
            m_has_response = true;
            Serial.printf("JetsonWSClient: received ← %s\n", m_response.c_str());
        }
        break;
    }

    default:
        break;
    }
}
