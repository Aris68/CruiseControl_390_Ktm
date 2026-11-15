#include "serial_ui.h"

#include <Arduino.h>
#include "can_bus.h"
#include "console.h"

static const uint32_t LIVE_PERIOD_MS   = 200;   // RPM/speed lines, 5 per second
static const uint32_t STATUS_PERIOD_MS = 20000;  // bus health line

// 0x55A is a slow frame, so the odometer gets a longer freshness window.
static const uint32_t ODOMETER_STALE_MS = 50000;

static uint32_t framesAtLastStatus = 0;
static uint32_t busErrorsAtLastStatus = 0;
static uint32_t lastLiveMs = 0;
static uint32_t lastStatusMs = 0;
static uint32_t lastTablePrintMs = 0;
static bool liveOutput = true;

// Every field has a fixed width that fits the largest value its CAN bytes can
// hold, and a stale value prints "--" in the same width. So every line of a
// given kind is the same length, and columns line up from one line to the next.

// Prints " | label text" with text right-aligned in width characters.
static void printField(const char *label, const char *text, int width) {
  console.printf(" | %s %*s", label, width, text);
}

// Readings print as raw numbers: switch bits as 0 or 1, whole bytes in hex.

// A numeric reading, formatted with fmt (e.g. "%.1f"), or "--" if stale.
static void printValue(const char *label, const Reading &reading,
                       const char *fmt, int width) {
  char text[16] = "--";
  if (reading.fresh()) snprintf(text, sizeof(text), fmt, reading.value);
  printField(label, text, width);
}

static void printRpm() {
  char text[8] = "--";
  if (rpm.fresh()) snprintf(text, sizeof(text), "%.0f", rpm.value);
  console.printf("RPM %5s", text);  // 16-bit: up to 65535
}

// Wheel speeds are 16-bit counts times a scale, so up to 6553.5 km/h.
static void printSpeed(const char *label, const Reading &reading) {
  printValue(label, reading, "%.1f", 6);
}

static void printCoolant() {
  printValue("coolant", coolant, "%.1f", 6);  // 16-bit / 10: up to 6553.5
  console.print(" C");
}

// A whole byte, printed in hex (e.g. 0x3C), or "--" if stale.
static void printHexByte(const char *label, const Reading &reading) {
  char text[8] = "--";
  if (reading.fresh()) snprintf(text, sizeof(text), "0x%02X", (uint8_t)reading.value);
  printField(label, text, 4);
}

static void printNumber(const char *label, const Reading &reading, int width) {
  printValue(label, reading, "%.0f", width);
}

// Second live line: the high-certainty signals from CAN_CODES.md.
static void printSignals() {
  char text[8] = "--";
  if (throttle.fresh()) snprintf(text, sizeof(text), "%.0f", throttle.value);
    console.printf("thr %3s", text);  // one byte: up to 255
  printNumber("grip", throttleClosed, 2);  // bit
  printNumber("clutch", clutch, 2);        // bit
  printNumber("kill", killRun, 2);         // bit
  printNumber("start", startButton, 2);    // bit
  printNumber("stand", sideStand, 2);      // bit
  printHexByte("TC", tractionControl);     // byte
  printHexByte("light", brakeLight);       // byte
  printNumber("gear", gear, 2);            // 4 bits: up to 15
  printNumber("brake", frontBrake, 5);     // 16-bit: up to 65535

  char odo[16] = "--";
  if (odometer.fresh(ODOMETER_STALE_MS)) {
    snprintf(odo, sizeof(odo), "%.1f", odometer.value);
  }

  printField("odo", odo, 9);  // 24-bit / 10: up to 1677721.5
  console.print(" km");
  console.println();
}

static void printLive() {
  printRpm();
  printSpeed("12B front", front12B);
  printSpeed("12B rear", rear12B);
  printSpeed("12D rear", rear12D);
  console.print(" km/h");
  printCoolant();
  console.println();
  printSignals();
}

static unsigned long capped(uint32_t value, uint32_t limit) {
  return (unsigned long)min(value, limit);
}

static void printStatus() {
  twai_status_info_t status;
  if (twai_get_status_info(&status) != ESP_OK) return;

  uint32_t newFrames = totalFrames - framesAtLastStatus;
  uint32_t newErrors = status.bus_error_count - busErrorsAtLastStatus;
  framesAtLastStatus = totalFrames;
  busErrorsAtLastStatus = status.bus_error_count;

  // Counters are capped so they never widen the line.
  console.printf("[bus] %5lu frames/s, %2d IDs, bus errors %7lu, lost %7lu",
                 capped(newFrames * 1000 / STATUS_PERIOD_MS, 99999), idCount,
                 capped(status.bus_error_count, 9999999),
                 capped(status.rx_missed_count + status.rx_overrun_count, 9999999));

  const char *hint = "";
  if (newFrames == 0) {
    hint = (newErrors > 0)
               ? "  <- errors but no frames: check bitrate, CANH/CANL order, ground"
               : "  <- bus silent: check ignition, wiring, CRX on GPIO3";
  }
  // Padded to the longest hint, so the line length is the same with or without one.
  console.printf("%-65s", hint);
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

    console.printf("%03lX %4lu/s ", (unsigned long)entry.id, capped(rate, 9999));
    for (int b = 0; b < 8; b++) {
      if (b < entry.dlc) {
        bool changed = entry.changed & (1 << b);
        console.printf("%02X%c ", entry.data[b], changed ? '*' : ' ');
      } else {
        console.print("    ");  // short frame: pad to 8 bytes
      }
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
      console.println(liveOutput ? "Live output on " : "Live output off");
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
    console.println();
    printLive();
  }
  if (now - lastStatusMs >= STATUS_PERIOD_MS) {
    lastStatusMs = now;
    printStatus();
  }
}
