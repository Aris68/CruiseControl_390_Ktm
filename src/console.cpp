#include "console.h"

#include <WiFi.h>
#include <errno.h>
#include <string.h>
#include <lwip/sockets.h>

Console console;

static WiFiServer server(TELNET_PORT);
static WiFiClient client;
static bool clientActive = false;

// Telnet clients send option negotiation (IAC sequences, starting with 0xFF)
// mixed in with what the user types. This strips them out.
enum TelnetState { TEXT, IAC, OPTION, SUBNEG, SUBNEG_IAC };
static TelnetState telnetState = TEXT;

static size_t txLength;

static void dropClient() {
  client.stop();
  clientActive = false;
  txLength = 0;
}

// ---------- Output -----------------------------------------------------------

// Wi-Fi output is collected here and sent once per loop, so a long print such
// as the ID table goes out in a few packets instead of hundreds.
static uint8_t txBuffer[4096];

static void flushTelnet() {
  if (!clientActive || txLength == 0) return;

  // Non-blocking send. If the Wi-Fi link is slow or out of range, nothing
  // waits on it: unsent text stays buffered and the CAN loop keeps running.
  int sent = send(client.fd(), txBuffer, txLength, MSG_DONTWAIT);
  if (sent < 0) {
    if (errno != EAGAIN && errno != EWOULDBLOCK) dropClient();
    return;
  }
  txLength -= sent;
  memmove(txBuffer, txBuffer + sent, txLength);
}

size_t Console::write(uint8_t c) {
  return write(&c, 1);
}

size_t Console::write(const uint8_t *buffer, size_t size) {
  Serial.write(buffer, size);

  if (clientActive) {
    if (txLength + size > sizeof(txBuffer)) flushTelnet();
    // Still full: the link cannot keep up, so this text is dropped.
    if (txLength + size <= sizeof(txBuffer)) {
      memcpy(txBuffer + txLength, buffer, size);
      txLength += size;
    }
  }
  return size;
}

// ---------- Input ------------------------------------------------------------

static int readTelnet() {
  while (clientActive && client.available() > 0) {
    int c = client.read();
    if (c < 0) return -1;

    switch (telnetState) {
      case TEXT:
        if (c == 0xFF) {
          telnetState = IAC;
        } else {
          return c;
        }
        break;
      case IAC:
        if (c == 0xFF) {  // escaped 0xFF data byte: not a command, ignore
          telnetState = TEXT;
        } else if (c == 0xFA) {  // subnegotiation start
          telnetState = SUBNEG;
        } else if (c >= 0xFB && c <= 0xFE) {  // WILL, WONT, DO, DONT + option
          telnetState = OPTION;
        } else {
          telnetState = TEXT;  // two-byte command
        }
        break;
      case OPTION:
        telnetState = TEXT;
        break;
      case SUBNEG:
        if (c == 0xFF) telnetState = SUBNEG_IAC;
        break;
      case SUBNEG_IAC:
        telnetState = (c == 0xF0) ? TEXT : SUBNEG;  // 0xF0 ends it
        break;
    }
  }
  return -1;
}

int Console::read() {
  if (Serial.available() > 0) {
    return Serial.read();
  }
  return readTelnet();
}

// ---------- Setup and update -------------------------------------------------

void consoleSetup() {
  Serial.begin(115200);
  delay(500);

  WiFi.mode(WIFI_AP);
  if (WiFi.softAP(WIFI_SSID, WIFI_PASSWORD)) {
    server.begin();
    server.setNoDelay(true);
    Serial.printf("Wi-Fi \"%s\" up. telnet %s %u\n", WIFI_SSID,
                  WiFi.softAPIP().toString().c_str(), TELNET_PORT);
  } else {
    Serial.println("Wi-Fi access point failed to start. USB serial only.");
  }
}

void consoleUpdate() {
  flushTelnet();

  if (clientActive && !client.connected()) {
    dropClient();
  }

  WiFiClient incoming = server.accept();
  if (incoming) {
    if (clientActive) {
      console.println("[wifi] another client connected, closing this one");
      flushTelnet();
      dropClient();
    }
    client = incoming;
    client.setNoDelay(true);
    clientActive = true;
    telnetState = TEXT;
    console.println("[wifi] telnet client connected");
    console.println("KTM 390 CAN reader. Send i for the ID table, l for live on/off.");
  }
}
