#pragma once
#include <Arduino.h>

// ---- Pins (LOLIN C3 Mini) ----
constexpr uint8_t PIN_RF_RX = 1;  // 433MHz receiver data
constexpr uint8_t PIN_RF_TX = 0;  // 433MHz transmitter data
constexpr uint8_t PIN_LED   = 7;  // onboard WS2812B RGB LED

constexpr uint8_t LED_BRIGHTNESS = 40;  // 0-255, keep it easy on the eyes

// ---- RF capture tuning ----
constexpr size_t   RAW_MAX_EDGES    = 320;   // max pulse durations per raw frame
constexpr uint32_t RAW_GAP_US       = 4500;  // silence longer than this ends a frame
constexpr uint16_t RAW_MIN_PULSE_US = 80;    // pulses shorter than this are noise
constexpr size_t   RAW_MIN_EDGES    = 24;    // frames with fewer edges are discarded
constexpr uint16_t RAW_FRAME_GAP_US = 9000;  // inter-frame gap when replaying raw

constexpr uint8_t DEFAULT_REPEATS = 10;      // remotes typically repeat ~10x per press

constexpr char FW_VERSION[]  = "0.1.0";
constexpr char AP_PASSWORD[] = "windowctl";  // setup AP password (min 8 chars)
