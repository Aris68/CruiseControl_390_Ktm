// Console: everything printed goes to the USB serial port and, when someone
// is connected, to a telnet client over Wi-Fi. Commands are read from both.
//
// The board runs its own Wi-Fi network, so no router is needed on the bike:
//   1. Join the network WIFI_SSID with password WIFI_PASSWORD.
//   2. Open a telnet connection to 192.168.4.1, port 23.
//      Windows: PuTTY, connection type "Telnet" (or "Raw").
//      Phone:   any telnet / TCP terminal app.
//   3. Type the same commands as in the Serial Monitor (i, l).
// One client at a time. A new connection replaces the old one.
#pragma once

#include <Arduino.h>

// ---------- Settings ---------------------------------------------------------

const char WIFI_SSID[]     = "KTM390-CAN";
const char WIFI_PASSWORD[] = "ktm390debug";  // at least 8 characters. Change it.
const uint16_t TELNET_PORT = 23;

// ---------- Console ----------------------------------------------------------

class Console : public Print {
public:
  size_t write(uint8_t c) override;
  size_t write(const uint8_t *buffer, size_t size) override;

  // Next command byte from USB serial or the telnet client, -1 if none.
  int read();
};

extern Console console;

// Starts USB serial and the Wi-Fi access point.
void consoleSetup();

// Accepts new telnet clients and notices dropped ones. Call it every loop.
void consoleUpdate();
