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

  bool fresh() const {
    return seen && (millis() - atMs) < STALE_MS;
  }
};

extern Reading rpm;
extern Reading front12B;
extern Reading rear12B;
extern Reading rear12D;
extern Reading coolant;

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
