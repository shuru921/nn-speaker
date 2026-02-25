#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <driver/i2s.h>
#include <esp_task_wdt.h>
#include <esp_heap_caps.h>
#include "I2SMicSampler.h"
#include "ADCSampler.h"
#include "I2SOutput.h"
#include "config.h"
#include "Application.h"
#include "SPIFFS.h"
#include "IntentProcessor.h"
#include "Speaker.h"
#include "IndicatorLight.h"
#include "AudioKitHAL.h"

// i2s config for using the internal ADC
i2s_config_t adcI2SConfig = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_ADC_BUILT_IN),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_MSB,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 64,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0};

// i2s config for reading from both channels of I2S
i2s_config_t i2sMemsConfigBothChannels = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_MIC_CHANNEL,
    .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 64,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0};

// i2s config for codec
i2s_config_t i2sCodecConfig = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 64,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0};

// i2s codec pins
i2s_pin_config_t i2s_codec_pins = {
    .bck_io_num = 5,
    .ws_io_num = 25,
    .data_out_num = 26,
    .data_in_num = 35};

// i2s microphone pins
i2s_pin_config_t i2s_mic_pins = {
    .bck_io_num = I2S_MIC_SERIAL_CLOCK,
    .ws_io_num = I2S_MIC_LEFT_RIGHT_CLOCK,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_MIC_SERIAL_DATA};

// i2s speaker pins
i2s_pin_config_t i2s_speaker_pins = {
    .bck_io_num = I2S_SPEAKER_SERIAL_CLOCK,
    .ws_io_num = I2S_SPEAKER_LEFT_RIGHT_CLOCK,
    .data_out_num = I2S_SPEAKER_SERIAL_DATA,
    .data_in_num = I2S_PIN_NO_CHANGE};

static Preferences g_wifiPrefs;
static String g_uartInputLine;

static String trimCopy(const String &in)
{
  String out = in;
  out.trim();
  return out;
}

static void loadWifiCredentials(String &ssid, String &password)
{
  g_wifiPrefs.begin("wifi", true);
  ssid = g_wifiPrefs.getString("ssid", WIFI_SSID);
  password = g_wifiPrefs.getString("pass", WIFI_PSWD);
  g_wifiPrefs.end();
}

static bool saveWifiCredentials(const String &ssid, const String &password)
{
  if (ssid.length() == 0)
  {
    return false;
  }

  g_wifiPrefs.begin("wifi", false);
  bool okSsid = g_wifiPrefs.putString("ssid", ssid) > 0;
  bool okPass = g_wifiPrefs.putString("pass", password) >= 0;
  g_wifiPrefs.end();
  return okSsid && okPass;
}

static void clearWifiCredentials()
{
  g_wifiPrefs.begin("wifi", false);
  g_wifiPrefs.clear();
  g_wifiPrefs.end();
}

static void processWifiUartCommand(const String &line)
{
  String command = trimCopy(line);
  if (command.length() == 0)
  {
    return;
  }

  if (command.equalsIgnoreCase("WIFI HELP"))
  {
    Serial.println("UART WiFi commands:");
    Serial.println("  WIFI SET <ssid>|<password>");
    Serial.println("  WIFI SHOW");
    Serial.println("  WIFI CLEAR");
    return;
  }

  if (command.equalsIgnoreCase("WIFI SHOW"))
  {
    String ssid;
    String password;
    loadWifiCredentials(ssid, password);
    Serial.printf("Stored SSID: %s\n", ssid.c_str());
    Serial.printf("Stored password length: %d\n", password.length());
    return;
  }

  if (command.equalsIgnoreCase("WIFI CLEAR"))
  {
    clearWifiCredentials();
    Serial.println("Stored WiFi credentials cleared.");
    return;
  }

  if (command.startsWith("WIFI SET "))
  {
    String payload = command.substring(strlen("WIFI SET "));
    int sep = payload.indexOf('|');
    if (sep <= 0)
    {
      Serial.println("Invalid format. Use: WIFI SET <ssid>|<password>");
      return;
    }

    String ssid = trimCopy(payload.substring(0, sep));
    String password = trimCopy(payload.substring(sep + 1));
    if (ssid.length() == 0)
    {
      Serial.println("SSID cannot be empty.");
      return;
    }

    if (saveWifiCredentials(ssid, password))
    {
      Serial.println("WiFi credentials saved.");
      Serial.println("Reboot to apply.");
    }
    else
    {
      Serial.println("Failed to save WiFi credentials.");
    }
    return;
  }

  Serial.println("Unknown command. Type WIFI HELP");
}

static void handleUartWifiProvisioning(const String &line)
{
  processWifiUartCommand(line);
}

void es8388_init(void)
{
    audiokit::AudioKit kit;
    auto cfg = kit.defaultConfig(audiokit::KitInputOutput);
    cfg.sample_rate = AUDIO_HAL_16K_SAMPLES;
    cfg.i2s_active = false;
    Serial.println("set AudioKit");
    kit.begin(cfg);
    kit.setVolume(100);
}

// This task does all the heavy lifting for our application
void applicationTask(void *param)
{
  Application *application = static_cast<Application *>(param);

  const TickType_t xMaxBlockTime = pdMS_TO_TICKS(100);
  while (true)
  {
    // wait for some audio samples to arrive
    uint32_t ulNotificationValue = ulTaskNotifyTake(pdTRUE, xMaxBlockTime);
    if (ulNotificationValue > 0)
    {
      application->run();
    }
  }
}

void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("Starting up");
  Serial.println("UART WiFi provisioning enabled. Type WIFI HELP and press Enter.");

#ifdef BOARD_HAS_PSRAM
  // Prefer external RAM for generic malloc to keep internal RAM for TLS handshake.
  heap_caps_malloc_extmem_enable(0);
#endif

  // start up wifi
  // launch WiFi
  String wifiSsid;
  String wifiPassword;
  loadWifiCredentials(wifiSsid, wifiPassword);
  Serial.printf("Connecting to SSID: %s\n", wifiSsid.c_str());

  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
  if (WiFi.waitForConnectResult() != WL_CONNECTED)
  {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    //ESP.restart();
  }
  Serial.printf("Total heap: %d\n", ESP.getHeapSize());
  Serial.printf("Free heap: %d\n", ESP.getFreeHeap());
  Serial.printf("Total PSRAM: %d\n", ESP.getPsramSize());
  Serial.printf("Free PSRAM: %d\n", ESP.getFreePsram());
  Serial.printf("Internal heap free: %u\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  Serial.printf("Internal heap largest block: %u\n", heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  Serial.printf("Chip model: %s\n", ESP.getChipModel());

  // startup SPIFFS for the wav files
  SPIFFS.begin();
  // make sure we don't get killed for our long running tasks
  esp_task_wdt_init(10, false);

  es8388_init();
  // start up the I2S input (from either an I2S microphone or Analogue microphone via the ADC)
#ifdef USE_I2S_MIC_INPUT
  // Direct i2s input from INMP441 or the SPH0645
  I2SSampler *i2s_sampler = new I2SMicSampler(i2s_codec_pins, false);
#else
  // Use the internal ADC
  I2SSampler *i2s_sampler = new ADCSampler(ADC_UNIT_1, ADC_MIC_CHANNEL);
#endif

  // start the i2s speaker output
  I2SOutput *i2s_output = new I2SOutput();
  i2s_output->start(I2S_NUM_0, i2s_codec_pins, i2sCodecConfig);
  Speaker *speaker = new Speaker(i2s_output);

  // indicator light to show when we are listening
  IndicatorLight *indicator_light = new IndicatorLight();

  // and the intent processor
  IntentProcessor *intent_processor = new IntentProcessor(speaker);
  /*
  intent_processor->addDevice("kitchen", GPIO_NUM_5);
  intent_processor->addDevice("bedroom", GPIO_NUM_21);
  intent_processor->addDevice("table", GPIO_NUM_23);
  */

  // create our application
  Application *application = new Application(i2s_sampler, intent_processor, speaker, indicator_light);

  // set up the i2s sample writer task
  TaskHandle_t applicationTaskHandle;
  xTaskCreate(applicationTask, "Application Task", 4096, application, 1, &applicationTaskHandle);

  // start sampling from i2s device - use I2S_NUM_0 as that's the one that supports the internal ADC
#ifdef USE_I2S_MIC_INPUT
  i2s_sampler->start(I2S_NUM_0, i2sCodecConfig, applicationTaskHandle);
#else
  i2s_sampler->start(I2S_NUM_0, adcI2SConfig, applicationTaskHandle);
#endif
}

void loop()
{
  while (Serial.available() > 0)
  {
    char ch = static_cast<char>(Serial.read());

    if (ch == '\r' || ch == '\n')
    {
      if (g_uartInputLine.length() > 0)
      {
        Serial.println();
        handleUartWifiProvisioning(g_uartInputLine);
        g_uartInputLine = "";
      }
      continue;
    }

    g_uartInputLine += ch;
    Serial.print(ch);
  }

  vTaskDelay(10);
}
