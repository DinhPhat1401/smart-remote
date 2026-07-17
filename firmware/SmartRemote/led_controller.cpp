/**
 * SmartRemote – led_controller.cpp
 * Điều khiển RGB LED: solid, blink, pattern theo DeviceMode
 */

#include "led_controller.h"

LEDController::LEDController(uint8_t pinR, uint8_t pinG, uint8_t pinB)
  : _pinR(pinR), _pinG(pinG), _pinB(pinB),
    _color(LEDColor::OFF), _pattern(LEDPattern::OFF),
    _intervalMs(500), _durationMs(0),
    _startMs(0), _lastToggleMs(0), _ledOn(false) {}

void LEDController::begin() {
  pinMode(_pinR, OUTPUT);
  pinMode(_pinG, OUTPUT);
  pinMode(_pinB, OUTPUT);
  _write(LEDColor::OFF);
}

// ─── update() – Gọi trong loop() ────────────────────────────
void LEDController::update() {
  uint32_t now = millis();

  // Kiểm tra hết duration → tắt
  if (_durationMs > 0 && (now - _startMs) >= _durationMs) {
    off();
    return;
  }

  switch (_pattern) {
    case LEDPattern::OFF:
      break;

    case LEDPattern::SOLID:
      _write(_color);
      break;

    case LEDPattern::BLINK:
    case LEDPattern::BLINK_FAST:
    case LEDPattern::BLINK_SLOW:
      if ((now - _lastToggleMs) >= _intervalMs) {
        _lastToggleMs = now;
        _ledOn = !_ledOn;
        _write(_ledOn ? _color : LEDColor::OFF);
      }
      break;

    case LEDPattern::PULSE:
      // Đơn giản hóa: dùng blink chậm nếu không có PWM
      if ((now - _lastToggleMs) >= 800) {
        _lastToggleMs = now;
        _ledOn = !_ledOn;
        _write(_ledOn ? _color : LEDColor::OFF);
      }
      break;
  }
}

// ─── solid() ─────────────────────────────────────────────────
void LEDController::solid(RGBColor color, uint32_t durationMs) {
  _color      = color;
  _pattern    = LEDPattern::SOLID;
  _durationMs = durationMs;
  _startMs    = millis();
  _write(color);
}

// ─── blink() ─────────────────────────────────────────────────
void LEDController::blink(RGBColor color, uint32_t intervalMs) {
  _color         = color;
  _pattern       = LEDPattern::BLINK;
  _intervalMs    = intervalMs;
  _durationMs    = 0;
  _startMs       = millis();
  _lastToggleMs  = 0;
  _ledOn         = true;
  _write(color);
}

void LEDController::blinkFast(RGBColor color) {
  blink(color, LED_BLINK_FAST_MS);
  _pattern = LEDPattern::BLINK_FAST;
}

void LEDController::blinkSlow(RGBColor color) {
  blink(color, LED_BLINK_SLOW_MS);
  _pattern = LEDPattern::BLINK_SLOW;
}

// ─── off() ───────────────────────────────────────────────────
void LEDController::off() {
  _pattern    = LEDPattern::OFF;
  _durationMs = 0;
  _write(LEDColor::OFF);
}

// ─── setMode() – Tự chọn màu + pattern theo DeviceMode ──────
void LEDController::setMode(DeviceMode mode) {
  switch (mode) {
    case MODE_IDLE:
      off();
      break;

    case MODE_BLUETOOTH:
      // 🔵 Xanh dương solid
      solid(LEDColor::BLUE);
      break;

    case MODE_WIFI:
    case MODE_INTERNET:
      // 🟢 Xanh lá solid
      solid(LEDColor::GREEN);
      break;

    case MODE_LEARN_WAIT:
      // 🔴 Đỏ chớp 500ms – Đang chờ học lệnh
      blink(LEDColor::RED, LED_BLINK_INTERVAL_MS);
      break;

    case MODE_LEARN_DONE:
      // 🔴 Đỏ solid 5s – Đã học xong
      solid(LEDColor::RED, LED_LEARN_DONE_MS);
      break;

    case MODE_SETUP:
      // ⚪ Trắng chớp chậm – AP Mode setup WiFi
      blinkSlow(LEDColor::WHITE);
      break;

    case MODE_OTA:
      // 🟣 Tím solid – OTA Update
      solid(LEDColor::PURPLE);
      break;

    default:
      off();
      break;
  }
}

// ─── _write() – Ghi trực tiếp ra GPIO ───────────────────────
void LEDController::_write(RGBColor color) {
  // Common cathode: HIGH = sáng | Common anode: đảo ngược
  analogWrite(_pinR, color.r);
  analogWrite(_pinG, color.g);
  analogWrite(_pinB, color.b);
}

void LEDController::_write(uint8_t r, uint8_t g, uint8_t b) {
  analogWrite(_pinR, r);
  analogWrite(_pinG, g);
  analogWrite(_pinB, b);
}
