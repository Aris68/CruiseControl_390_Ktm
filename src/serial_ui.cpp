#include "serial_ui.h"

#include <Arduino.h>
#include "can_bus.h"
#include "console.h"

static const uint32_t LIVE_PERIOD_MS   = 200;   // RPM/speed lines, 5 per second
static const uint32_t STATUS_PERIOD_MS = 2000;  // bus health line

static uint32_t framesAtLastStatus = 0;
static uint32_t busErrorsAtLastStatus = 0;
static uint32_t lastLiveMs = 0;
static uint32_t lastStatusMs = 0;
static uint32_t lastTablePrintMs = 0;
static bool liveOutput = true;

static void printRpm() {
  if (rpm.fresh()) {
    console.printf("RPM %5.0f", rpm.value);
  } else {
    console.print("RPM    --");
  }
}

static void printSpeed(const char *label, const Reading &reading) {
  if (reading.fresh()) {
    console.printf(" | %s %5.1f", label, reading.value);
  } else {
    console.printf(" | %s    --", label);
  }
}

static void printCoolant() {
  if (coolant.fresh()) {
    console.printf(" | coolant %5.1f C", coolant.value);
  } else {
    console.print(" | coolant    --");
  }
}

static void printLive() {
  printRpm();
  printSpeed("12B front", front12B);
  printSpeed("12B rear", rear12B);
  printSpeed("12D rear", rear12D);
  console.print(" km/h");
  printCoolant();
  console.println();
}

static void printStatus() {
  twai_status_info_t status;
  if (twai_get_status_info(&status) != ESP_OK) return;

  uint32_t newFrames = totalFrames - framesAtLastStatus;
  uint32_t newErrors = status.bus_error_count - busErrorsAtLastStatus;
  framesAtLastStatus = totalFrames;
  busErrorsAtLastStatus = status.bus_error_count;

  console.printf("[bus] %lu frames/s, %d IDs, bus errors %lu, lost %lu",
                (unsigned long)(newFrames * 1000 / STATUS_PERIOD_MS), idCount,
                (unsigned long)status.bus_error_count,
                (unsigned long)(status.rx_missed_count + status.rx_overrun_count));

  if (newFrames == 0) {
    if (newErrors > 0) {
      console.print("  <- errors but no frames: check bitrate, CANH/CANL order, ground");
    } else {
      console.print("  <- bus silent: check ignition, wiring, CRX on GPIO3");
    }
  }
  console.println();
}

static void printIdTable() {
  uint32_t now = millis();
  uint32_t elapsedMs = now - lastTablePrintMs;
  lastTablePrintMs = now;

  console.println();
  console.printf("--- %d IDs, * = changed since the last table ---\n", idCount);
  console.println("ID    rate  D0  D1  D2  D3  D4  D5  D6  D7");

  for (int i = 0; i < idCount; i++) {
    IdEntry &entry = idTable[i];
    uint32_t newFrames = entry.count - entry.countAtLastPrint;
    uint32_t rate = (elapsedMs > 0) ? (newFrames * 1000 / elapsedMs) : 0;

    console.printf("%03lX %4lu/s ", (unsigned long)entry.id, (unsigned long)rate);
    for (int b = 0; b < entry.dlc; b++) {
      bool changed = entry.changed & (1 << b);
      console.printf("%02X%c ", entry.data[b], changed ? '*' : ' ');
    }
    console.println();

    entry.countAtLastPrint = entry.count;
    entry.changed = 0;
  }

  if (idTableFull) {
    console.println("Table full: raise MAX_IDS to see the remaining IDs");
  }
  console.println();
}

static void handleCommand() {
  int command;
  while ((command = console.read()) >= 0) {
    if (command == 'i') {
      printIdTable();
    } else if (command == 'l') {
      liveOutput = !liveOutput;
      console.println(liveOutput ? "Live output on" : "Live output off");
    }
  }
}

void uiSetup() {
  lastTablePrintMs = millis();
}

void uiUpdate() {
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
