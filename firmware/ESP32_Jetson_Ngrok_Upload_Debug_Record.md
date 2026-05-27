# ESP32 錄音上傳 Jetson Orin 問題紀錄

## 專案目標

本專案目標是讓 ESP32 在偵測到 wake word 後開始錄音，將音訊包成 WAV，並用 `multipart/form-data` POST 到 Jetson Orin 上的 ASR 服務。

預期流程：

1. ESP32 透過 I2S 麥克風持續收音。
2. Wake word model 偵測到喚醒詞。
3. ESP32 錄製一段語音。
4. ESP32 將錄音轉成 WAV multipart request。
5. 透過 ngrok HTTPS 傳到 Jetson Orin：

```text
ESP32 -> https://xxx.ngrok-free.app/process -> Jetson Orin ASR server
```

---

## 目前使用設定

`src/config.h` 中 Jetson/ngrok 設定：

```cpp
#define JETSON_USE_HTTPS true
#define JETSON_HOST "7f9e-61-216-173-1.ngrok-free.app"
#define JETSON_PORT 443
```

代表目前是使用 ngrok HTTPS 模式，而不是區域網路 HTTP 模式。

---

## 問題一：HTTP port 80 無法送到 Jetson

### 現象

ESP32 使用 HTTP port 80 時，程式顯示 `write()` 成功，但是：

- Jetson server 沒有收到 request log
- ngrok dashboard 也沒有 request 紀錄

### 原因

ngrok 免費版提供的公開網址主要是 HTTPS。若 ESP32 對 port 80 發 HTTP request，ngrok 只會回傳 301 redirect 到 HTTPS，不會直接轉發到後端 Jetson server。

ESP32 原本的程式沒有處理 redirect，因此 request 不會成功抵達 `/process`。

### 結論

ngrok 模式必須使用：

```text
HTTPS port 443
```

不能只用 HTTP port 80。

---

## 問題二：HTTP 打到錯誤 port 導致 connection reset

### 現象

曾經出現：

```text
errno: 104, Connection reset by peer
```

### 原因

當時設定其實是用普通 `WiFiClient` 發 HTTP，但連到 port 443。port 443 是 HTTPS/TLS，不接受純 HTTP 明文資料，因此 server 直接 reset connection。

### 結論

設定必須一致：

| 模式 | Client | Port |
|---|---|---|
| HTTP | `WiFiClient` | 80 或 Jetson 內網 port，例如 8000 |
| HTTPS | `WiFiClientSecure` | 443 |

---

## 問題三：HTTPS + PSRAM malloc 策略導致 TLS 握手失敗

### 現象

使用：

```cpp
heap_caps_malloc_extmem_enable(0);
```

時，曾出現：

```text
SSL - The connection indicated an EOF
```

### 原因

`heap_caps_malloc_extmem_enable(0)` 會讓大多數 malloc 都優先進 PSRAM。

但是 ESP32 的 mbedTLS / AES 硬體加速與 DMA 對記憶體位置有限制，部分 TLS I/O buffer 若被配置到 PSRAM，可能導致 TLS 封包處理異常。server 端收到不正常封包後會關閉連線，因此 ESP32 看到 EOF。

### 結論

不能讓所有 TLS 相關 malloc 都進 PSRAM。

後來改成：

```cpp
heap_caps_malloc_extmem_enable(32768);
```

讓較大的配置才進 PSRAM，TLS 小型結構仍留在 internal RAM。

---

## 問題四：HTTPS TLS internal RAM 不足

### 現象

Wake word 偵測成功後，進入 ASR 錄音流程：

```text
[WakeWord] score=1.00
P(1.00): Here I am, brain the size of a planet...
Free ram before DetectWakeWord cleanup 4190123
Free ram after DetectWakeWord cleanup 4227819
[ASR] Recording...
i2s Reader Task stopped
[ASR] Internal heap before HTTPS: free=83812 largest=42996
[E][ssl_client.cpp] SSL - Memory allocation failed
[Jetson] HTTPS connect failed: 7f9e-61-216-173-1.ngrok-free.app:443
```

後續再停掉 I2S output writer task 與 I2S driver，並降低 Application Task stack 後，得到的新 log：

```text
[WakeWord] score=0.98
P(0.98): Here I am, brain the size of a planet...
Free ram before DetectWakeWord cleanup 4198243
Free ram after DetectWakeWord cleanup 4235931
[ASR] Recording...
i2s Reader Task stopped
i2s Writer Task stopped
i2s output driver stopped
[ASR] Internal heap before HTTPS: free=99692 largest=42996
[E][ssl_client.cpp] SSL - Memory allocation failed
[Jetson] HTTPS connect failed: 7f9e-61-216-173-1.ngrok-free.app:443
[ASR] Internal heap after HTTPS: free=98632 largest=42996
RX task started
TX start
```

### 重要觀察

雖然 log 中的 free heap 看起來有 4MB：

```text
[ASR] Free heap: 4226759
```

但這主要是 PSRAM，不代表 mbedTLS 可以使用。

真正重要的是 internal heap：

```text
[ASR] Internal heap before HTTPS: free=83812 largest=42996
```

其中 `largest=42996` 表示最大連續 internal RAM 區塊只有約 42KB。

後續測試中，即使 internal free heap 增加到接近 100KB：

```text
free=99692 largest=42996
```

最大連續區塊 `largest` 仍然固定約 42KB。這代表瓶頸不是 internal RAM 總量，而是記憶體碎片化或系統配置導致 mbedTLS 拿不到足夠大的連續區塊。

### 原因

目前 Arduino-ESP32 預編譯的 mbedTLS 設定為：

```text
CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC=y
CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN=16384
```

代表 TLS 記憶體必須從 internal RAM 配置，而且 TLS record buffer 預設為 16KB。

HTTPS 建立時，mbedTLS 至少需要：

- input TLS buffer 約 16KB
- output TLS buffer 約 16KB
- handshake 結構
- certificate / entropy / RNG / socket 相關結構

因此即使總 internal free 有 83KB，只要最大連續區塊只有 42KB，仍可能在：

```cpp
mbedtls_ssl_setup()
```

階段 malloc 失敗。

### 已做修正

為了釋放 internal RAM，目前已做：

1. Wake word 偵測結束後釋放 NeuralNetwork 與 AudioProcessor。
2. Tensor arena 改用 16-byte aligned allocation，避免 TensorFlow Micro alignment warning。
3. 錄音完成、HTTPS 前暫停 I2S reader task：

```text
i2s Reader Task stopped
```

4. 進一步在 HTTPS 前暫停 I2S output writer task 與 I2S driver：

```text
i2s Writer Task stopped
i2s output driver stopped
```

5. HTTPS 後再恢復 I2S reader/output，讓下一輪 wake word 可以繼續運作。
6. 將 Application Task stack 從 16KB 降到 8KB，希望增加 mbedTLS 可用的連續 internal heap。

### 最新測試結論

即使已經釋放 I2S reader、I2S writer、I2S output driver，並降低 Application Task stack，仍然出現：

```text
[ASR] Internal heap before HTTPS: free=99692 largest=42996
SSL - Memory allocation failed
```

這代表總 free internal heap 已經增加，但最大連續區塊沒有增加。mbedTLS 在 `mbedtls_ssl_setup()` 階段仍無法配置 TLS 所需的大型連續 buffer。

因此可以判斷，目前卡住的不是 Jetson server、ngrok URL、multipart 格式或錄音流程，而是：

```text
ESP32 + Arduino framework + ngrok HTTPS 在目前配置下受限於 mbedTLS internal RAM fragmentation。
```

繼續單純釋放 task 或 I2S buffer，效果有限。

### 目前結論

目前主要卡點是：

```text
ESP32 internal RAM 最大連續區塊不足，導致 ngrok HTTPS TLS 建立失敗。
```

這不是 Jetson server 沒開，也不是 ngrok endpoint 錯，而是 ESP32 端 HTTPS TLS 記憶體需求過高。

更精確地說，是 mbedTLS 需要較大的「連續 internal RAM」，而不是只需要較高的總 free heap。

---

## 問題五：一開始以為卡在 wake word，實際是啟動流程與 task 問題

### 現象

曾經看到：

```text
Ready - waiting for wake word...
Starting i2s
```

然後沒有後續反應。

### 後來發現

log 中其實有：

```text
Connection Failed! Rebooting...
```

但當時程式把：

```cpp
ESP.restart();
```

註解掉了，所以 WiFi 失敗後還繼續跑，造成後面狀態混亂。

此外，原本 Application 建構時就載入 NeuralNetwork，可能先吃掉 internal RAM，導致後續 task 建立不穩。

### 已做修正

1. WiFi 連線失敗時真的重開機：

```cpp
ESP.restart();
```

2. 印出 WiFi 狀態與 IP：

```text
WiFi connected, IP: ...
```

3. 印出 task 建立失敗訊息：

```text
Application task create failed
i2s Reader Task create failed
```

4. 延後 `Application::begin()`，讓 task 建好後才載入 wake word model。

---

## 問題六：multipart upload 原本使用 chunked encoding

### 原本作法

原本 ESP32 上傳音訊時使用：

```http
Transfer-Encoding: chunked
```

### 風險

ngrok 與後端框架，例如 FastAPI 或 Flask，對 chunked multipart upload 的支援可能不如一般 `Content-Length` request 穩定。

### 已做修正

改成明確計算：

```http
Content-Length: ...
```

並仍然分段寫出音訊資料，避免 ESP32 一次在 RAM 中組出完整 request body。

目前 request 格式改為：

```http
POST /process HTTP/1.1
Host: 7f9e-61-216-173-1.ngrok-free.app
ngrok-skip-browser-warning: true
Content-Type: multipart/form-data; boundary=----ESP32Boundary
Content-Length: ...
Connection: close
```

---

## 目前程式改善摘要

### Wake word cleanup

修正前有重複或誤導性的記憶體 log。現在只在真正離開 wake word state 時釋放：

- AudioProcessor
- NeuralNetwork
- Tensor arena

### TensorFlow Micro tensor arena

修正前：

```cpp
m_tensor_arena = (uint8_t *)malloc(kArenaSize);
```

可能出現：

```text
4 bytes lost due to alignment
```

修正後：

```cpp
m_tensor_arena = (uint8_t *)heap_caps_aligned_alloc(16, kArenaSize, MALLOC_CAP_8BIT);
```

並用：

```cpp
heap_caps_free(m_tensor_arena);
```

釋放。

### HTTPS 前釋放資源

HTTPS 上傳前暫停：

- I2S reader task
- I2S writer task
- I2S output driver

目標是讓 mbedTLS 有較大的 internal RAM 連續區塊。

---

## 建議後續解法

### 解法 A：改用內網 HTTP 直連 Jetson

這是最穩定、最推薦的方案。

ESP32 和 Jetson 在同一個 WiFi / LAN 內時，設定：

```cpp
#define JETSON_USE_HTTPS false
#define JETSON_HOST "Jetson 的區網 IP"
#define JETSON_PORT 8000
```

優點：

- 不需要 TLS
- 不吃 mbedTLS internal RAM
- 不需要大型連續 internal RAM 給 HTTPS handshake
- 不經 ngrok，延遲較低
- Jetson server log 最容易 debug

缺點：

- 只能在同一區域網路使用
- 不適合跨網路 demo

### 解法 B：使用中介伺服器

架構：

```text
ESP32 --HTTP--> 中介伺服器 --HTTPS/ngrok--> Jetson
```

ESP32 只負責簡單 HTTP，TLS 壓力交給中介伺服器。

優點：

- ESP32 不需要處理 HTTPS
- 可保留跨網路能力

缺點：

- 多一個 server 要維護

### 解法 B-2：透過 VPN 但仍使用 HTTP

如果需要跨網路，但又想避免 ngrok HTTPS，可以使用 VPN gateway。重點是 ESP32 不一定要自己跑 VPN，而是讓路由器、電腦或手機熱點負責 VPN。

可行架構：

```text
ESP32 -> WiFi router / laptop / phone hotspot -> VPN -> Jetson HTTP
```

ESP32 端仍然只送普通 HTTP：

```cpp
#define JETSON_USE_HTTPS false
#define JETSON_HOST "VPN 裡 Jetson 可達的 IP"
#define JETSON_PORT 8000
```

這樣一樣可以避開 mbedTLS，因為 ESP32 沒有建立 HTTPS 連線。

需要確認：

- Jetson server listen `0.0.0.0:8000`
- VPN 或防火牆允許 TCP port 8000
- ESP32 所在網路能路由到 Jetson 的 VPN IP
- 如果 VPN 只裝在筆電上，ESP32 不會自動走 VPN，除非筆電被設定成 gateway 或網路分享來源

### 解法 C：改用 ESP-IDF 自訂 mbedTLS 設定

若一定要 ESP32 直接 HTTPS 連 ngrok，可以考慮改 ESP-IDF project，自訂 mbedTLS：

- 降低 `MBEDTLS_SSL_MAX_CONTENT_LEN`
- 啟用 variable buffer length
- 減少憑證與 cipher suite

優點：

- 保留 ESP32 直接 HTTPS

缺點：

- 工程改動大
- PlatformIO Arduino framework 較難調整預編譯 mbedTLS
- 需要重新測試 TLS 相容性

---

## 心得整理

這次問題的困難點不是單一 bug，而是 ESP32、ngrok、TLS、PSRAM、I2S audio pipeline 同時互相影響。

一開始看起來像是 Jetson 沒收到 request，但實際上問題分成很多層：

1. HTTP/HTTPS port 設定錯誤會直接造成 redirect 或 connection reset。
2. ngrok 免費版主要要求 HTTPS。
3. ESP32 的 PSRAM 雖然很大，但 mbedTLS 不一定能使用 PSRAM。
4. 真正影響 HTTPS 成敗的是 internal RAM，尤其是最大連續可配置區塊。
5. 音訊任務、I2S driver、wake word model 都會佔用 internal RAM。
6. 要 debug ESP32 記憶體問題，不能只看 `free heap`，還要看：

```cpp
heap_caps_get_free_size(MALLOC_CAP_INTERNAL)
heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)
```

這次最具代表性的測試結果是：

```text
free=99692 largest=42996
```

這表示即使 internal free heap 接近 100KB，因為最大連續區塊只有約 42KB，HTTPS 仍然失敗。因此在 ESP32 上處理 HTTPS 時，「最大連續記憶體」比「總剩餘記憶體」更關鍵。

這次最大的收穫是：嵌入式系統的錯誤常常不是「程式邏輯錯」，而是硬體資源限制、記憶體配置策略、網路協定行為共同造成的結果。尤其在 ESP32 上使用 HTTPS 時，PSRAM 不能完全取代 internal RAM，因此設計時要盡量避免在 ESP32 端承擔太重的 TLS 或大檔案傳輸壓力。

---

## 目前狀態

截至目前，wake word 偵測、錄音流程、multipart request 組裝都已經能執行。

目前主要未解問題是：

```text
ESP32 透過 ngrok HTTPS 連線時，mbedTLS internal RAM allocation failed。
```

後續建議優先測試內網 HTTP 直連 Jetson，確認 ASR server 與音訊格式完全正常後，再決定是否需要繼續挑戰 ESP32 直連 ngrok HTTPS。
