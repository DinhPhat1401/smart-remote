/**
 * SmartRemote – led_controller.h
 * Điều khiển RGB LED (3 pin riêng lẻ)
 */

#pragma once
#include <Arduino.h>
#include "config.h"

// Màu sắc định nghĩa sẵn (R, G, B)
struct RGBColor {
  uint8_t r, g, b;
};

namespace LEDColor {
  constexpr RGBColor OFF        = {0,   0,   0};
  constexpr RGBColor RED        = {255, 0,   0};
  constexpr RGBColor GREEN      = {0,   255, 0};
  constexpr RGBColor BLUE       = {0,   0,   255};
  constexpr RGBColor YELLOW     = {255, 200, 0};
  constexpr RGBColor ORANGE     = {255, 80,  0};
  constexpr RGBColor WHITE      = {255, 255, 255};
  constexpr RGBColor PURPLE     = {150, 0,   255};
  constexpr RGBColor CYAN       = {0,   200, 255};
}

enum class LEDPattern {
  OFF,
  SOLID,        // Sáng liên tục
  BLINK,        // Chớp theo interval
  BLINK_FAST,   // Chớp nhanh
  BLINK_SLOW,   // Chớp chậm
  PULSE,        // Nhấp nháy mờ dần (PWM nếu hỗ trợ)
};

class LEDController {
public:
  LEDController(uint8_t pinR, uint8_t pinG, uint8_t pinB);
  
  void begin();
  void update();  // Gọi trong loop()

  // Sáng một màu liên tục (duration = 0 → vĩnh viễn)
  void solid(RGBColor color, uint32_t durationMs = 0);

  // Chớp theo pattern
  void blink(RGBColor color, uint32_t intervalMs = LED_BLINK_INTERVAL_MS);
  void blinkFast(RGBColor color);
  void blinkSlow(RGBColor color);

  // Tắt LED
  void off();

  // Set trực tiếp theo DeviceMode (tự chọn màu + pattern)
  void setMode(DeviceMode mode);

private:
  uint8_t _pinR, _pinG, _pinB;
  
  RGBColor  _color;
  LEDPattern _pattern;
  uint32_t  _intervalMs;
  uint32_t  _durationMs;
  uint32_t  _startMs;
  uint32_t  _lastToggleMs;
  bool      _ledOn;

  void _write(RGBColor color);
  void _write(uint8_t r, uint8_t g, uint8_t b);
};
