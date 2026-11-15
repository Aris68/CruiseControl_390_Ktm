/*
  KTM 390 Adventure (2020) - passive CAN reader
  Board:       ESP32-C6-DevKitM-1
  Transceiver: SN65HVD230
  Output:      live line over Serial (115200 baud) and Wi-Fi telnet:
               RPM, wheel speed, gear, clutch, TC, ABS mode, ride mode,
               front/rear brake, side stand

  Files:
    can_bus    TWAI driver, frame decoding, table of every CAN ID
    serial_ui  Serial Monitor output and commands
    console    output to USB serial and to a telnet client over Wi-Fi

  CAN IDs and bits are listed in CAN_CODES.md. Wheel speed (0x12D) uses a
  guessed scale: calibrate SCALE_12D_KMH in can_bus.h against GPS.
  To find a new signal, use the ID table (command 'i'), then edit
  decodeFrame() in can_bus.cpp.

  Serial Monitor commands (send the letter, then Enter):
    i  print every CAN ID seen. '*' marks bytes that changed since the last 'i'
    l  turn the live line on or off

  Wireless: the board runs a Wi-Fi network "KTM390-CAN" (password in
  console.h). Join it and telnet to 192.168.4.1 port 23 for the same output
  and commands as the Serial Monitor.

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
#include "console.h"
#include "serial_ui.h"

void setup() {
  consoleSetup();
  console.println("KTM 390 CAN reader, listen-only");

  if (!canSetup()) {
    console.println("TWAI start failed. Check the board selection and pins.");
    while (true) {
      delay(1000);
    }
  }

  console.println("Listening. Send i for the ID table, l for live on/off.");
  uiSetup();
}

void loop() {
  canPoll();
  consoleUpdate();
  uiUpdate();
}
