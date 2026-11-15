#include "can_bus.h"

#include <string.h>

// 500 kbit/s is confirmed
#define CAN_TIMING TWAI_TIMING_CONFIG_500KBITS()

Reading rpm;
Reading wheelSpeed;
Reading gear;
Reading clutch;
Reading tractionControl;
Reading absOffroad;
Reading rideOffroad;
Reading frontBrake;
Reading rearBrake;
Reading sideStand;

IdEntry idTable[MAX_IDS];
int idCount = 0;
bool idTableFull = false;
uint32_t totalFrames = 0;

// ---------- Frame handling ---------------------------------------------------

// CAN signals here are big-endian: the first byte is the high byte.
static uint16_t be16(const uint8_t *data, int index) {
  return (uint16_t)((data[index] << 8) | data[index + 1]);
}

// 1 if any bit of mask is set, else 0.
static float flag(uint8_t byte, uint8_t mask) {
  return (byte & mask) ? 1 : 0;
}

static void decodeFrame(const twai_message_t &msg) {
  if (msg.extd) return;  // the known KTM IDs are all 11-bit

  const uint8_t *d = msg.data;
  const int length = msg.data_length_code;

  switch (msg.identifier) {
    case 0x120:  // D0-D1: engine speed, 1 rpm per count
      if (length >= 2) rpm.set(be16(d, 0));
      break;

    case 0x129:  // D0 bits 4-7: gear, D0 bit 3: clutch
      if (length >= 1) {
        gear.set(d[0] >> 4);
        clutch.set(flag(d[0], 0x08));
      }
      break;

    case 0x12A:  // D1: traction control, D2: ABS mode, D4: ride mode
      if (length >= 5) {
        tractionControl.set(flag(d[1], 0x20));  // D1 = 0x2D on, 0x1D off
        absOffroad.set(flag(d[2], 0x02));
        rideOffroad.set(flag(d[4], 0x20));
      }
      break;

    case 0x12D:  // D1-D2: rear wheel speed
      if (length >= 3) wheelSpeed.set(be16(d, 1) * SCALE_12D_KMH);
      break;

    case 0x12F:  // D1 bit 6: rear brake
      if (length >= 2) rearBrake.set(flag(d[1], 0x40));
      break;

    case 0x290:  // D0: front brake pressure, 0 at rest
      if (length >= 1) frontBrake.set(d[0] > 0 ? 1 : 0);
      break;

    case 0x540:  // D3 bit 0: side stand
      if (length >= 4) sideStand.set(flag(d[3], 0x01));
      break;
  }
}

static void updateIdTable(const twai_message_t &msg) {
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

static void handleFrame(const twai_message_t &msg) {
  if (msg.rtr) return;  // remote frames carry no data

  totalFrames++;
  updateIdTable(msg);
  decodeFrame(msg);
}

// ---------- Public functions -------------------------------------------------

bool canSetup() {
  // Listen-only: the controller never transmits and never sends ACK or error
  // frames. The ECU, ABS and dash acknowledge each other's frames as usual.
  twai_general_config_t generalConfig =
      TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_LISTEN_ONLY);
  generalConfig.tx_queue_len = 0;    // nothing is ever sent
  generalConfig.rx_queue_len = 256;  // holds frames while Serial is printing

  twai_timing_config_t timingConfig = CAN_TIMING;
  twai_filter_config_t filterConfig = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  return twai_driver_install(&generalConfig, &timingConfig, &filterConfig) == ESP_OK &&
         twai_start() == ESP_OK;
}

void canPoll() {
  // Empty the receive queue first, so printing never delays decoding for long.
  twai_message_t msg;
  if (twai_receive(&msg, pdMS_TO_TICKS(2)) == ESP_OK) {
    do {
      handleFrame(msg);
    } while (twai_receive(&msg, 0) == ESP_OK);
  }
}
