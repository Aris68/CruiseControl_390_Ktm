/*
  KTM 390 Adventure (2020) - passive CAN reader
  Board:       ESP32-C6-DevKitM-1
  Transceiver: SN65HVD230
  Output:      RPM and wheel speed over Serial at 115200 baud

  Files:
    can_bus    TWAI driver, frame decoding, table of every CAN ID
    serial_ui  Serial Monitor output and commands

  THE CAN IDs ARE NOT CONFIRMED ON THE 2020 BIKE.
  They come from other KTMs:
    0x120 RPM          690 Enduro R, 1390 Super Duke, 2026 Adventure 390 (confirmed)
    0x12B wheel speed  690 Enduro R, 701 Enduro (not sent on the 2026 Adventure 390)
    0x12D wheel speed  2026 Adventure 390 (scale is a guess)
  Both speed candidates are decoded. If neither responds, use the ID table
  (command 'i') to find the right frame, then edit decodeFrame() in can_bus.cpp.

  Serial Monitor commands (send the letter, then Enter):
    i  print every CAN ID seen. '*' marks bytes that changed since the last 'i'
    l  turn the live RPM/speed lines on or off

  USB: with "USB CDC On Boot" disabled, use the port marked UART
       (with it enabled, use the other USB port).

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

#include <Arduino.h>
#include "can_bus.h"
#include "serial_ui.h"

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("KTM 390 CAN reader, listen-only");

  if (!canSetup()) {
    Serial.println("TWAI start failed. Check the board selection and pins.");
    while (true) {
      delay(1000);
    }
  }

  Serial.println("Listening. Send i for the ID table, l for live on/off.");
  uiSetup();
}

void loop() {
  canPoll();
  uiUpdate();
}
