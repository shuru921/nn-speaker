// WiFi credentials
#define WIFI_SSID "hsc48"
#define WIFI_PSWD "04124572"

// are you using an I2S microphone - comment this out if you want to use an analog mic and ADC input
#define USE_I2S_MIC_INPUT

// I2S Microphone Settings

// Which channel is the I2S microphone on? I2S_CHANNEL_FMT_ONLY_LEFT or I2S_CHANNEL_FMT_ONLY_RIGHT
#define I2S_MIC_CHANNEL I2S_CHANNEL_FMT_ONLY_LEFT
// #define I2S_MIC_CHANNEL I2S_CHANNEL_FMT_ONLY_RIGHT
#define I2S_MIC_SERIAL_CLOCK GPIO_NUM_33
#define I2S_MIC_LEFT_RIGHT_CLOCK GPIO_NUM_26
#define I2S_MIC_SERIAL_DATA GPIO_NUM_25

// Analog Microphone Settings - ADC1_CHANNEL_7 is GPIO35
#define ADC_MIC_CHANNEL ADC1_CHANNEL_7

// speaker settings
#define I2S_SPEAKER_SERIAL_CLOCK GPIO_NUM_14
#define I2S_SPEAKER_LEFT_RIGHT_CLOCK GPIO_NUM_12
#define I2S_SPEAKER_SERIAL_DATA GPIO_NUM_27

// Jetson Orin ASR 服務設定
// 模式 A：區域網路（HTTP）
//   JETSON_USE_HTTPS false, HOST = IP, PORT = 8000
// 模式 B：Demo 跨網路（HTTPS via ngrok）
//   JETSON_USE_HTTPS true, HOST = ngrok domain, PORT = 443

#define JETSON_USE_HTTPS true
#define JETSON_HOST "8b9e-61-216-173-101.ngrok-free.app"
#define JETSON_PORT 443

// 錄音觸發按鈕：LyraT v4.3 的 REC 觸控鍵（GPIO36 = PIN_KEY1）
// GPIO36 是 input-only，不支援 INPUT_PULLUP，見 Application.cpp
#define RECORD_BUTTON_PIN GPIO_NUM_36

// command recognition settings (Wit.ai，已改為 Jetson，保留備用)
//#define COMMAND_RECOGNITION_ACCESS_KEY "P5QMUSMFV6IRRSTABXFQ7UIXPFRMC4L5"
#define COMMAND_RECOGNITION_ACCESS_KEY "N6HHFVXOVIZFCLAPEMC7LPDZ4KXONLSJ"
