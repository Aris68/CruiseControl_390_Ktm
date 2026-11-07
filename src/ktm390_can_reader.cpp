/*
  KTM 390 Adventure (2020) - passive CAN reader
  Board:       ESP32-C6-DevKitM-1
  Transceiver: SN65HVD230
  Output:      RPM and wheel speed over Serial at 115200 baud

  THE CAN IDs BELOW ARE NOT CONFIRMED ON THE 2020 BIKE.
  They come from other KTMs:
    0x120 RPM          690 Enduro R, 1390 Super Duke, 2026 Adventure 390 (confirmed)
    0x12B wheel speed  690 Enduro R, 701 Enduro (not sent on the 2026 Adventure 390)
    0x12D wheel speed  2026 Adventure 390 (scale is a guess)
  The sketch decodes both speed candidates. If neither responds, use the ID
  table (command 'i') to find the right frame, then edit decodeFrame().

  Serial Monitor commands (send the letter, then Enter):
    i  print every CAN ID seen. '*' marks bytes that changed since the last 'i'
    l  turn the live RPM/speed lines on or off

  Arduino IDE settings:
    Board: ESP32C6 Dev Module (esp32 by Espressif, core 3.x)
    USB CDC On Boot: Disabled, with the cable in the port marked UART
                     (use Enabled if the cable is in the other USB port)

  Wiring, for this read-only stage:
    SN65HVD230 3V3  -> ESP32 3V3
    SN65HVD230 GND  -> ESP32 GND and bike ground
    SN65HVD230 CRX  -> ESP32 GPIO3
    SN65HVD230 CTX  -> 3V3, NOT to a GPIO. This holds the transmitter off in
                       hardware, so the board cannot disturb the ECU or ABS
                       even if the firmware misbehaves.
    SN65HVD230 CANH -> bike CAN-H
    SN65HVD230 CANL -> bike CAN-L
    ESP32 GPIO2     -> leave unconnected
*/

#include "driver/twai.h"
#include <Arduino.h>
#include <stdlib.h>
#include <stdint.h>


// ---------- Settings ---------------------------------------------------------

// GPIO2 and GPIO3 are not strapping pins and sit next to 3V3 on header J1.
// The driver needs a TX pin assigned, but nothing is connected to it.
const gpio_num_t CAN_TX_PIN = GPIO_NUM_2;
const gpio_num_t CAN_RX_PIN = GPIO_NUM_3;

// 500 kbit/s is confirmed on the 2026 Adventure 390. If the status line shows
// bus errors and no frames, try TWAI_TIMING_CONFIG_250KBITS().
#define CAN_TIMING TWAI_TIMING_CONFIG_500KBITS()

// Starting values. Calibrate against GPS at a steady speed:
//   new scale = old scale * (GPS speed / printed speed)
const float SCALE_12B_KMH = 0.05627f;  // 701 Enduro: 17.77 counts per km/h
const float SCALE_12D_KMH = 0.1f;      // 2026 Adventure 390: a guess

const uint32_t LIVE_PERIOD_MS   = 200;   // RPM/speed lines, 5 per second
const uint32_t STATUS_PERIOD_MS = 2000;  // bus health line
const uint32_t STALE_MS         = 500;   // show "--" if a value is older
const int      MAX_IDS          = 64;    // size of the ID table

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

Reading rpm;
Reading front12B;
Reading rear12B;
Reading rear12D;

// ---------- Table of every ID on the bus -------------------------------------

struct IdEntry {
  uint32_t id;
  uint32_t count;
  uint32_t countAtLastPrint;
  uint8_t dlc;
  uint8_t data[8];
  uint8_t changed;  // one bit per byte, set if it changed since the last print
};

IdEntry idTable[MAX_IDS];
int idCount = 0;
bool idTableFull = false;

uint32_t totalFrames = 0;
uint32_t framesAtLastStatus = 0;
uint32_t busErrorsAtLastStatus = 0;
uint32_t lastLiveMs = 0;
uint32_t lastStatusMs = 0;
uint32_t lastTablePrintMs = 0;
bool liveOutput = true;

// ---------- Frame handling ---------------------------------------------------

// CAN signals here are big-endian: the first byte is the high byte.
uint16_t be16(const uint8_t *data, int index) {
  return (uint16_t)((data[index] << 8) | data[index + 1]);
}

void decodeFrame(const twai_message_t &msg) {
  if (msg.extd) return;  // the known KTM IDs are all 11-bit

  switch (msg.identifier) {
    case 0x120:  // D0-D1: engine speed, 1 rpm per count
      if (msg.data_length_code >= 2) {
        rpm.set(be16(msg.data, 0));
      }
      break;

    case 0x12B:  // D0-D1: front wheel, D2-D3: rear wheel
      if (msg.data_length_code >= 4) {
        front12B.set(be16(msg.data, 0) * SCALE_12B_KMH);
        rear12B.set(be16(msg.data, 2) * SCALE_12B_KMH);
      }
      break;

    case 0x12D:  // D1-D2: rear wheel
      if (msg.data_length_code >= 3) {
        rear12D.set(be16(msg.data, 1) * SCALE_12D_KMH);
      }
      break;
  }
}

void updateIdTable(const twai_message_t &msg) {
  int slot = -1;
  for (int i = 0; i < idCount; i++) {
    if (idTable[i].id == msg.identifier) {
      slot = i;
      break;
    }
  }

  if (slot < 0) {
    if (idCount >= MAX_IDS) {
      idTableFull = true;
      return;
    }
    slot = idCount++;
    idTable[slot] = IdEntry{};
    idTable[slot].id = msg.identifier;
  }

  IdEntry &entry = idTable[slot];
  int length = min((int)msg.data_length_code, 8);

  // The first frame of an ID is the baseline, not a change.
  if (entry.count > 0) {
    for (int i = 0; i < length; i++) {
      if (entry.data[i] != msg.data[i]) {
        entry.changed |= (1 << i);
      }
    }
  }

  memcpy(entry.data, msg.data, length);
  entry.dlc = length;
  entry.count++;
}

void handleFrame(const twai_message_t &msg) {
  if (msg.rtr) return;  // remote frames carry no data

  totalFrames++;
  updateIdTable(msg);
  decodeFrame(msg);
}

// ---------- Serial output ----------------------------------------------------

void printRpm() {
  if (rpm.fresh()) {
    Serial.printf("RPM %5.0f", rpm.value);
  } else {
    Serial.print("RPM    --");
  }
}

void printSpeed(const char *label, const Reading &reading) {
  if (reading.fresh()) {
    Serial.printf(" | %s %5.1f", label, reading.value);
  } else {
    Serial.printf(" | %s    --", label);
  }
}

void printLive() {
  printRpm();
  printSpeed("12B front", front12B);
  printSpeed("12B rear", rear12B);
  printSpeed("12D rear", rear12D);
  Serial.println(" km/h");
}

void printStatus() {
  twai_status_info_t status;
  if (twai_get_status_info(&status) != ESP_OK) return;

  uint32_t newFrames = totalFrames - framesAtLastStatus;
  uint32_t newErrors = status.bus_error_count - busErrorsAtLastStatus;
  framesAtLastStatus = totalFrames;
  busErrorsAtLastStatus = status.bus_error_count;

  Serial.printf("[bus] %lu frames/s, %d IDs, bus errors %lu, lost %lu",
                (unsigned long)(newFrames * 1000 / STATUS_PERIOD_MS), idCount,
                (unsigned long)status.bus_error_count,
                (unsigned long)(status.rx_missed_count + status.rx_overrun_count));

  if (newFrames == 0) {
    if (newErrors > 0) {
      Serial.print("  <- errors but no frames: check bitrate, CANH/CANL order, ground");
    } else {
      Serial.print("  <- bus silent: check ignition, wiring, CRX on GPIO3");
    }
  }
  Serial.println();
}

void printIdTable() {
  uint32_t now = millis();
  uint32_t elapsedMs = now - lastTablePrintMs;
  lastTablePrintMs = now;

  Serial.println();
  Serial.printf("--- %d IDs, * = changed since the last table ---\n", idCount);
  Serial.println("ID    rate  D0  D1  D2  D3  D4  D5  D6  D7");

  for (int i = 0; i < idCount; i++) {
    IdEntry &entry = idTable[i];
    uint32_t newFrames = entry.count - entry.countAtLastPrint;
    uint32_t rate = (elapsedMs > 0) ? (newFrames * 1000 / elapsedMs) : 0;

    Serial.printf("%03lX %4lu/s ", (unsigned long)entry.id, (unsigned long)rate);
    for (int b = 0; b < entry.dlc; b++) {
      bool changed = entry.changed & (1 << b);
      Serial.printf("%02X%c ", entry.data[b], changed ? '*' : ' ');
    }
    Serial.println();

    entry.countAtLastPrint = entry.count;
    entry.changed = 0;
  }

  if (idTableFull) {
    Serial.println("Table full: raise MAX_IDS to see the remaining IDs");
  }
  Serial.println();
}

void handleCommand() {
  while (Serial.available() > 0) {
    char command = Serial.read();
    if (command == 'i') {
      printIdTable();
    } else if (command == 'l') {
      liveOutput = !liveOutput;
      Serial.println(liveOutput ? "Live output on" : "Live output off");
    }
  }
}

// ---------- Setup and main loop ----------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("KTM 390 CAN reader, listen-only");

  // Listen-only: the controller never transmits and never sends ACK or error
  // frames. The ECU, ABS and dash acknowledge each other's frames as usual.
  twai_general_config_t generalConfig =
      TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_LISTEN_ONLY);
  generalConfig.tx_queue_len = 0;    // nothing is ever sent
  generalConfig.rx_queue_len = 256;  // holds frames while Serial is printing

  twai_timing_config_t timingConfig = CAN_TIMING;
  twai_filter_config_t filterConfig = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&generalConfig, &timingConfig, &filterConfig) != ESP_OK ||
      twai_start() != ESP_OK) {
    Serial.println("TWAI start failed. Check the board selection and pins.");
    while (true) {
      delay(1000);
    }
  }

  Serial.println("Listening. Send i for the ID table, l for live on/off.");
  lastTablePrintMs = millis();
}

void loop() {
  // Empty the receive queue first, so printing never delays decoding for long.
  twai_message_t msg;
  if (twai_receive(&msg, pdMS_TO_TICKS(2)) == ESP_OK) {
    do {
      handleFrame(msg);
    } while (twai_receive(&msg, 0) == ESP_OK);
  }

  handleCommand();

  uint32_t now = millis();
  if (liveOutput && now - lastLiveMs >= LIVE_PERIOD_MS) {
    lastLiveMs = now;
    printLive();
  }
  if (now - lastStatusMs >= STATUS_PERIOD_MS) {
    lastStatusMs = now;
    printStatus();
  }
}
