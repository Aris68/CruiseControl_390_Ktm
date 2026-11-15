#include "serial_ui.h"

#include <Arduino.h>
#include "can_bus.h"
#include "console.h"

static const uint32_t LIVE_PERIOD_MS   = 200;    // live line, 5 per second
static const uint32_t STATUS_PERIOD_MS = 20000;  // bus health line

static uint32_t lastLiveMs = 0;
static uint32_t lastStatusMs = 0;
static uint32_t lastTablePrintMs = 0;
static uint32_t framesAtLastStatus = 0;
static uint32_t busErrorsAtLastStatus = 0;
static bool liveOutput = true;

// ---------- Live line --------------------------------------------------------

// One value on the live line. width fits the largest value the CAN bytes can
// hold, so the line is always the same length and the columns line up.
struct Field {
  const char *label;
  const Reading &reading;
  int decimals;
  int width;
  const char *unit;
};

// Switches print their raw bit: 0 or 1.
static const Field LIVE_LINE[] = {
  {"RPM",    rpm,             0, 5, ""},       // 16-bit
  {"speed",  wheelSpeed,      1, 6, " km/h"},  // 16-bit x scale
  {"gear",   gear,            0, 2, ""},       // 4 bits
  {"clutch", clutch,          0, 2, ""},       // 1 = pulled
  {"TC",     tractionControl, 0, 2, ""},       // 1 = on
  {"ABS",    absOffroad,      0, 2, ""},       // 1 = offroad
  {"mode",   rideOffroad,     0, 2, ""},       // 1 = offroad
  {"fBrake", frontBrake,      0, 2, ""},       // 1 = pressed
  {"rBrake", rearBrake,       0, 2, ""},       // 1 = pressed
  {"stand",  sideStand,       0, 2, ""},       // 1 = up
};

// Prints the fields separated by " | ". A stale value prints as "--".
static void printLive() {
  const size_t count = sizeof(LIVE_LINE) / sizeof(LIVE_LINE[0]);
  for (size_t i = 0; i < count; i++) {
    const Field &f = LIVE_LINE[i];
    char text[16] = "--";
    if (f.reading.fresh()) snprintf(text, sizeof(text), "%.*f", f.decimals, f.reading.value);
    console.printf("%s%s %*s%s", i > 0 ? " | " : "", f.label, f.width, text, f.unit);
  }
  console.println();
}

// ---------- Bus status line --------------------------------------------------

// Keeps a counter from widening the line.
static unsigned long capped(uint32_t value, uint32_t limit) {
  return min(value, limit);
}

static void printStatus() {
  twai_status_info_t status;
  if (twai_get_status_info(&status) != ESP_OK) return;

  uint32_t newFrames = totalFrames - framesAtLastStatus;
  uint32_t newErrors = status.bus_error_count - busErrorsAtLastStatus;
  framesAtLastStatus = totalFrames;
  busErrorsAtLastStatus = status.bus_error_count;

  const char *hint = "";
  if (newFrames == 0) {
    hint = (newErrors > 0)
               ? "  <- errors but no frames: check bitrate, CANH/CANL order, ground"
               : "  <- bus silent: check ignition, wiring, CRX on GPIO3";
  }

  // The hint is padded to the longest one, so the length never changes.
  console.printf("[bus] %5lu frames/s, %2d IDs, bus errors %7lu, lost %7lu%-65s",
                 capped(newFrames * 1000 / STATUS_PERIOD_MS, 99999), idCount,
                 capped(status.bus_error_count, 9999999),
                 capped(status.rx_missed_count + status.rx_overrun_count, 9999999),
                 hint);
}

// ---------- ID table ('i') ---------------------------------------------------

static void printIdTable() {
  uint32_t now = millis();
  uint32_t elapsedMs = now - lastTablePrintMs;
  lastTablePrintMs = now;

  console.printf("\n--- %d IDs, * = changed since the last table ---\n", idCount);
  console.println("ID    rate  D0  D1  D2  D3  D4  D5  D6  D7");

  for (int i = 0; i < idCount; i++) {
    IdEntry &entry = idTable[i];
    uint32_t rate = elapsedMs ? (entry.count - entry.countAtLastPrint) * 1000 / elapsedMs : 0;

    console.printf("%03lX %4lu/s ", (unsigned long)entry.id, capped(rate, 9999));
    for (int b = 0; b < 8; b++) {
      if (b < entry.dlc) {
        console.printf("%02X%c ", entry.data[b], (entry.changed & (1 << b)) ? '*' : ' ');
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

// ---------- Public functions -------------------------------------------------

void uiSetup() {
  lastTablePrintMs = millis();
}

void uiUpdate() {
  int command;
  while ((command = console.read()) >= 0) {
    if (command == 'i') {
      printIdTable();
    } else if (command == 'l') {
      liveOutput = !liveOutput;
      console.println(liveOutput ? "Live output on " : "Live output off");
    }
  }

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
