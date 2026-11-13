// CAN bus: TWAI driver in listen-only mode, frame decoding, and a table of
// every ID seen on the bus.
#pragma once

#include <Arduino.h>
#include <stdint.h>
#include "driver/twai.h"

// ---------- Settings ---------------------------------------------------------

// GPIO2 and GPIO3 are not strapping pins and sit next to 3V3 on header J1.
// The driver needs a TX pin assigned, but nothing is connected to it.
const gpio_num_t CAN_TX_PIN = GPIO_NUM_2;
const gpio_num_t CAN_RX_PIN = GPIO_NUM_3;

// Starting values. Calibrate against GPS at a steady speed:
//   new scale = old scale * (GPS speed / printed speed)
const float SCALE_12B_KMH = 0.05627f;  // 701 Enduro: 17.77 counts per km/h
const float SCALE_12D_KMH = 0.1f;      // 2026 Adventure 390: a guess

const uint32_t STALE_MS = 500;  // a value older than this is not fresh
const int      MAX_IDS  = 64;   // size of the ID table

// ---------- Decoded values ---------------------------------------------------

// Remembers when each value arrived, so an old number is never printed as if
// it were current. The cruise control will need the same check.
struct Reading {
  float value = 0;
  uint32_t atMs = 0;
  bool seen = false;

  void set(float v) {
    value = v;
    atMs = millis();
    seen = true;
  }

  bool fresh(uint32_t maxAgeMs = STALE_MS) const {
    return seen && (millis() - atMs) < maxAgeMs;
  }
};

extern Reading rpm;
extern Reading front12B;
extern Reading rear12B;
extern Reading rear12D;
extern Reading coolant;

// High-certainty signals, see CAN_CODES.md. Switches hold 0 or 1.
extern Reading throttle;         // 0x120 D2: 0 closed .. 253 open
extern Reading killRun;          // 0x120 D3 bit 0x08: 1 = RUN
extern Reading startButton;      // 0x120 D3 bit 0x10: 1 while pressed
extern Reading throttleClosed;   // 0x128 D2 bit 0x01: 1 = grip closed
extern Reading gear;             // 0x129 D0 bits 4-7
extern Reading clutch;           // 0x129 D0 bit 0x08, raw bit
extern Reading tractionControl;  // 0x12A D1, raw byte: TC_ON or TC_OFF
extern Reading frontBrake;       // 0x290 D0-D1, raw pressure counts
extern Reading brakeLight;       // 0x290 D3, raw byte: BRAKE_LIGHT_ON or _OFF
extern Reading sideStand;        // 0x540 D3 bit 0x01: 1 = up
extern Reading odometer;         // 0x55A D0-D2, km

const uint8_t TC_ON           = 0x28;
const uint8_t TC_OFF          = 0x18;
const uint8_t BRAKE_LIGHT_ON  = 0x0E;  // confirmed for the front lever only
const uint8_t BRAKE_LIGHT_OFF = 0x02;

// ---------- Table of every ID on the bus -------------------------------------

struct IdEntry {
  uint32_t id;
  uint32_t count;
  uint32_t countAtLastPrint;
  uint8_t dlc;
  uint8_t data[8];
  uint8_t changed;  // one bit per byte, set if it changed since the last print
};

extern IdEntry idTable[MAX_IDS];
extern int idCount;
extern bool idTableFull;
extern uint32_t totalFrames;

// ---------- Functions --------------------------------------------------------

// Installs and starts the driver. Returns false if that failed.
bool canSetup();

// Empties the receive queue, decoding every frame. Call it every loop.
void canPoll();
