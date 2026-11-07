#include "can_bus.h"

#include <string.h>

// 500 kbit/s is confirmed
#define CAN_TIMING TWAI_TIMING_CONFIG_500KBITS()

Reading rpm;
Reading front12B;
Reading rear12B;
Reading rear12D;
Reading coolant;

IdEntry idTable[MAX_IDS];
int idCount = 0;
bool idTableFull = false;
uint32_t totalFrames = 0;

// ---------- Frame handling ---------------------------------------------------

// CAN signals here are big-endian: the first byte is the high byte.
static uint16_t be16(const uint8_t *data, int index) {
  return (uint16_t)((data[index] << 8) | data[index + 1]);
}

static void decodeFrame(const twai_message_t &msg) {
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

    case 0x540:  // D6-D7: coolant temperature, 0.1 C per count
      if (msg.data_length_code >= 8) {
        coolant.set(be16(msg.data, 6) / 10.0f);
      }
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
