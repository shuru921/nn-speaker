#ifndef _jetson_ws_client_h_
#define _jetson_ws_client_h_

#include <Arduino.h>
#include <WebSocketsClient.h>

class JetsonWSClient
{
public:
    JetsonWSClient(const char *host, uint16_t port);

    // Call once to initiate connection
    void connect();

    // Must be called regularly (from a dedicated FreeRTOS task)
    void loop();

    // Send recognised text as a command to Jetson
    void sendCommand(const String &text);

    // Returns true if Jetson has sent a response
    bool hasResponse() const { return m_has_response; }

    // Returns the response text and clears the flag
    String takeResponse();

    // True when WebSocket is currently connected
    bool isConnected() const { return m_connected; }

private:
    WebSocketsClient m_ws;
    const char *m_host;
    uint16_t m_port;

    volatile bool m_connected;
    volatile bool m_has_response;
    String m_response;

    void onEvent(WStype_t type, uint8_t *payload, size_t length);
};

#endif
