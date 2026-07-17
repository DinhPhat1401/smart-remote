# SmartRemote – Firmware Libraries

Cài đặt các thư viện sau trong Arduino IDE (Tools → Manage Libraries):

| Thư viện | Tác giả | Version |
|---------|---------|---------|
| IRremoteESP8266 | crankyoldgit | >= 2.8.6 |
| ArduinoJson | Benoit Blanchon | 6.x |
| NimBLE-Arduino | h2zero | >= 1.4.0 |
| ESPAsyncWebServer | lacamera | >= 3.x |
| AsyncTCP | me-no-dev | >= 1.1 |
| WiFiManager | tzapu | >= 2.0 (ESP32) |
| WebSockets | Markus Sattler | >= 2.3.7 |

## Board Settings (Arduino IDE)

- **Board**: ESP32 Dev Module
- **Partition Scheme**: Default 4MB with SPIFFS (hoặc FFAT)
- **Upload Speed**: 921600
- **Flash Mode**: QIO
- **Flash Frequency**: 80MHz

## Cấu hình trước khi Upload

Mở `config.h` và thay:
```cpp
#define SUPABASE_URL    "https://YOUR-PROJECT.supabase.co"
#define SUPABASE_ANON_KEY "your-anon-key"
```

## Upload

1. Mở `SmartRemote.ino` trong Arduino IDE
2. Chọn đúng COM port
3. Click Upload
4. Sau khi upload, mở Serial Monitor (115200 baud)
5. ESP32 sẽ vào AP Mode với tên `SmartRemote-XXXX`
6. Kết nối WiFi trên điện thoại vào AP đó để cấu hình
