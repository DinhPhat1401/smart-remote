# SmartRemote 🎮

> Thiết bị IoT học và phát tín hiệu hồng ngoại (IR), điều khiển qua Bluetooth, WiFi hoặc Internet.

## Tính năng

- 📡 **Học lệnh IR** – Thu nhận tín hiệu từ remote hồng ngoại bất kỳ
- 📤 **Phát lệnh IR** – Điều khiển TV, điều hòa, quạt, v.v.
- 📱 **3 chế độ kết nối**: BLE · WiFi LAN · Internet (Supabase)
- 🎨 **Tạo remote ảo** – Kéo thả nút bấm theo layout tùy chỉnh
- 📦 **Import IRDB** – Nhập dữ liệu IR từ cơ sở dữ liệu IRDB
- 🌐 **Website + Android APK** – Một codebase, đa nền tảng

## Phần cứng

| Linh kiện | Vai trò |
|-----------|---------|
| ESP32 | Vi điều khiển chính |
| TSOP1738 | Bộ thu hồng ngoại |
| IR LED | Bộ phát hồng ngoại |
| RGB LED | Hiển thị trạng thái |
| Button | Chuyển chế độ / Reset WiFi |
| Công tắc gạt | Bật/tắt nguồn |

## Cấu trúc dự án

```
SmartRemote/
├── firmware/          # Arduino/C++ – Nạp vào ESP32
│   └── SmartRemote/
├── supabase/          # Database schema & Edge Functions
│   ├── migrations/
│   └── functions/
└── app/               # React + Vite + Capacitor
    │                  # → Website + Android APK
    ├── src/
    ├── android/
    └── ...
```

## Kết nối ESP32 – GPIO

| GPIO | Linh kiện |
|------|-----------|
| 15 | TSOP1738 (IR Receiver) |
| 4 | IR LED (qua transistor 2N2222) |
| 25 | RGB LED – Red |
| 26 | RGB LED – Green |
| 27 | RGB LED – Blue |
| 0 | Button (Boot) |

## LED States

| Màu | Pattern | Ý nghĩa |
|-----|---------|---------|
| 🔴 Đỏ | Chớp 500ms | Chờ học lệnh IR |
| 🔴 Đỏ | Solid 5s | Đã học xong |
| 🔵 Xanh dương | Solid | Chế độ Bluetooth |
| 🟢 Xanh lá | Solid | WiFi / Internet |
| ⚪ Trắng | Chớp chậm | Setup WiFi (AP Mode) |
| 🟡 Vàng | Chớp nhanh | Đang kết nối |

## Tech Stack

| Layer | Technology |
|-------|-----------|
| Firmware | Arduino C++, IRremoteESP8266, NimBLE, ESPAsyncWebServer |
| Database | Supabase (PostgreSQL + Realtime + Auth) |
| App | React + Vite + TypeScript + Capacitor.js |
| Android | Capacitor Android Plugin, `@capacitor-community/bluetooth-le` |

## Cài đặt & Build

### Firmware
1. Mở `firmware/SmartRemote/SmartRemote.ino` bằng Arduino IDE
2. Cài thư viện: `IRremoteESP8266`, `NimBLE-Arduino`, `ArduinoJson`, `ESPAsyncWebServer`, `WiFiManager`
3. Cấu hình `config.h` với Supabase URL & Key
4. Upload lên ESP32

### Supabase
```bash
# Cài Supabase CLI
npm install -g supabase
supabase login
supabase db push
supabase functions deploy irdb-import
```

### App (Website + Android)
```bash
cd app
npm install

# Chạy dev server
npm run dev

# Build website
npm run build

# Build Android APK
npx cap sync android
npx cap open android    # Mở Android Studio
```

## License

MIT © 2025 SmartRemote Project
