/*
  Signal finder: a guided test that tells you what to press on the bike and
  works out which CAN ID, byte and bits follow that control.

  This is a separate test firmware. It lives in test/ and changes nothing in
  src/. It only borrows two things from the main program, unmodified:
    src/console.cpp   the Wi-Fi telnet + USB console
    include/can_bus.h the CAN pin numbers
  Like the main program it is listen-only and never transmits.

  Build and flash it instead of the main firmware:
    pio run -e signal_finder -t upload
  Go back to the main firmware:
    pio run -e esp32-c6-devkitm-1 -t upload

  Use it: join the Wi-Fi network from console.h, telnet to 192.168.4.1 port 23
  (or open the USB serial monitor) and send t.

  How a step works:
    The control is alternated between a rest state and an active state
    (released / held, OFF / ON, mode A / mode B). Every frame is sorted into
    "rest" or "active" samples, leaving out the moments right after each
    change. Then every bit of every byte of every ID is checked:

    STRONG  the bit never moved during rest, never moved during active, and
            has a different value in the two. This is the signal, or a
            checksum/copy that follows it.
    ANALOG  the byte's value range during active does not overlap its range
            during rest (pressure, throttle position, ...).
    PULSE   the bit is steady at rest and moved at some point during the
            action (a button that only sends a short event, a blinker).
*/

#include <Arduino.h>
#include <string.h>
#include "driver/twai.h"
#include "can_bus.h"   // CAN_TX_PIN, CAN_RX_PIN, MAX_IDS only
#include "console.h"

// Every ID seen on the bus. A frame's position here is its slot in stats[].
static uint32_t seenIds[MAX_IDS];
static int seenCount = 0;

// ---------- Timing -----------------------------------------------------------

// HOLD steps run on a timer. The first part of every phase is thrown away,
// because that is when your hand is still moving.
static const uint32_t HOLD_SETTLE_MS = 1500;
static const uint32_t HOLD_SAMPLE_MS = 2000;

// STATE steps wait for Enter, then sample for longer so slow frames show up.
static const uint32_t STATE_SETTLE_MS = 1000;
static const uint32_t STATE_SAMPLE_MS = 4000;

// "\r\n" from a terminal is one Enter, not two.
static const uint32_t ENTER_DEBOUNCE_MS = 300;

// ---------- Steps ------------------------------------------------------------

enum Mode : uint8_t {
  HOLD,   // timed: hold on the prompt, release on the prompt
  STATE,  // you set state A or B, then press Enter
};

struct Step {
  const char *name;
  Mode mode;
  uint8_t phases;      // rest, active, rest, ... (odd phases are active)
  const char *rest;    // HOLD: how to release.  STATE: state A
  const char *active;  // HOLD: what to hold.    STATE: state B
  const char *note;    // extra advice, or what is already known
};

static const Step steps[] = {
    // --- Left handlebar menu buttons ---
    {"Menu button UP", HOLD, 7, "release the button",
     "press and HOLD the UP menu button",
     "The dash menu will move around while you test the buttons. That is fine."},
    {"Menu button DOWN", HOLD, 7, "release the button",
     "press and HOLD the DOWN menu button", nullptr},
    {"Menu button SET", HOLD, 7, "release the button",
     "press and HOLD the SET button", nullptr},
    {"Menu button RETURN", HOLD, 7, "release the button",
     "press and HOLD the RETURN button", nullptr},

    // --- Lights and horn ---
    {"High beam (rest = LOW beam, active = HIGH beam)", HOLD, 7,
     "switch back to LOW beam", "switch to HIGH beam",
     "Low beam is the rest value of whatever this finds."},
    {"Headlight flasher (pass trigger)", HOLD, 7, "release the trigger",
     "pull and HOLD the pass / flash trigger", nullptr},
    {"Left turn signal", HOLD, 7, "cancel the turn signal",
     "switch the LEFT turn signal on",
     "A blinking lamp shows up as PULSE, the switch itself as STRONG."},
    {"Right turn signal", HOLD, 7, "cancel the turn signal",
     "switch the RIGHT turn signal on", nullptr},
    {"Hazard lights", HOLD, 7, "switch the hazards off",
     "switch the hazards on", "Skip if the bike has no hazard switch."},
    {"Horn", HOLD, 7, "release the horn", "press and HOLD the horn", nullptr},

    // --- Controls the cruise control must watch ---
    {"Front brake lever", HOLD, 7, "release the lever",
     "squeeze and HOLD the front brake",
     "Known so far: 0x290 D0-D1 pressure. 0x290 D3 brake light is wrong."},
    {"Rear brake pedal", HOLD, 7, "release the pedal",
     "press and HOLD the rear brake pedal", nullptr},
    {"Clutch lever", HOLD, 7, "release the lever",
     "pull and HOLD the clutch lever", "Known so far: 0x129 D0 bit 0x08."},
    {"Throttle grip", HOLD, 7, "let the grip close fully",
     "open the throttle fully and HOLD it (engine OFF)",
     "Known so far: 0x120 D2 and 0x128 D2 bit 0x01."},
    // No start button step: it would crank the engine with the kill switch at
    // RUN, and with it at OFF most frames stop. 0x120 D3 bit 0x10 is verified.

    // --- Things you set, then confirm with Enter ---
    // No kill switch step: most frames stop when it is OFF, so every step
    // runs with it at RUN. 0x120 D3 bit 0x08 is the current best guess.
    {"Side stand", STATE, 3, "side stand DOWN", "side stand UP (hold the bike!)",
     "Known so far: 0x540 D3 bit 0x01."},
    {"Gear: neutral / 1st", STATE, 3, "gearbox in NEUTRAL", "gearbox in 1st gear",
     "Engine off, rock the bike to help it engage. Known: 0x129 D0 bits 4-7."},
    {"Traction control (MTC)", STATE, 3, "MTC switched ON", "MTC switched OFF",
     "Change it in the dash menu, close the menu, then press Enter. "
     "Known so far: 0x12A D1."},
    {"ABS mode", STATE, 3, "ABS mode ROAD", "ABS mode OFFROAD",
     "Change it in the dash menu, close the menu, then press Enter."},
    {"Ride mode: Street / Rain", STATE, 3, "ride mode STREET", "ride mode RAIN",
     "Skip if the bike has no ride modes."},
    {"Ride mode: Street / Offroad", STATE, 3, "ride mode STREET",
     "ride mode OFFROAD", "Skip if the bike has no ride modes."},
    {"Quickshifter", STATE, 3, "Quickshifter+ ON", "Quickshifter+ OFF",
     "Skip if not fitted."},
    {"Heated grips", STATE, 3, "heated grips OFF", "heated grips ON",
     "Skip if not fitted."},
    {"Engine running", STATE, 3, "engine OFF, ignition on",
     "engine IDLING in neutral",
     "RPM and temperatures will show up too. Look for single bits."},
    // Nothing here needs the bike to move. The ABS warning lamp (goes out
    // after riding a few metres) is left for later.
};

static const int STEP_COUNT = sizeof(steps) / sizeof(steps[0]);

// ---------- Per-ID statistics for the running step ---------------------------

struct Stat {
  uint32_t restCount, actCount, anyCount;
  uint8_t dlc;
  uint8_t restAnd[8], restOr[8], restMin[8], restMax[8];  // settled rest
  uint8_t actAnd[8], actOr[8], actMin[8], actMax[8];      // settled active
  uint8_t anyAnd[8], anyOr[8];  // whole active phase, hand movement included
};

static Stat stats[MAX_IDS];

static void resetStats() {
  for (int i = 0; i < MAX_IDS; i++) {
    Stat &s = stats[i];
    s.restCount = s.actCount = s.anyCount = 0;
    s.dlc = 0;
    memset(s.restAnd, 0xFF, 8);
    memset(s.restOr, 0x00, 8);
    memset(s.restMin, 0xFF, 8);
    memset(s.restMax, 0x00, 8);
    memset(s.actAnd, 0xFF, 8);
    memset(s.actOr, 0x00, 8);
    memset(s.actMin, 0xFF, 8);
    memset(s.actMax, 0x00, 8);
    memset(s.anyAnd, 0xFF, 8);
    memset(s.anyOr, 0x00, 8);
  }
}

// ---------- Results ----------------------------------------------------------

enum Kind : uint8_t { K_STRONG, K_ANALOG, K_PULSE, K_ONLY_ACTIVE, K_ONLY_REST };

struct Finding {
  uint16_t id;
  Kind kind;
  uint8_t byte;
  uint8_t mask;        // K_STRONG, K_PULSE
  uint8_t a, b, c, d;  // K_STRONG: rest, active.  K_ANALOG: rest min,max, act min,max
};

static const int MAX_FINDINGS = 64;   // per step, all kept for the summary
static const int PRINT_LIMIT  = 24;   // lines printed right after a step

enum StepStatus : uint8_t { NOT_RUN, SKIPPED, DONE };

struct StepResult {
  StepStatus status;
  bool overflow;  // more hits than MAX_FINDINGS
  uint8_t count;
  Finding findings[MAX_FINDINGS];  // best evidence first
};

static StepResult results[STEP_COUNT];

static void printFinding(const Finding &f) {
  switch (f.kind) {
    case K_STRONG:
      console.printf("  STRONG  0x%03X D%d mask 0x%02X : rest 0x%02X -> active 0x%02X\n",
                     f.id, f.byte, f.mask, f.a, f.b);
      break;
    case K_ANALOG:
      console.printf("  ANALOG  0x%03X D%d           : rest %d..%d -> active %d..%d\n",
                     f.id, f.byte, f.a, f.b, f.c, f.d);
      break;
    case K_PULSE:
      console.printf("  PULSE   0x%03X D%d mask 0x%02X : steady at rest, moves during the action\n",
                     f.id, f.byte, f.mask);
      break;
    case K_ONLY_ACTIVE:
      console.printf("  NOTE    0x%03X is only sent during the action\n", f.id);
      break;
    case K_ONLY_REST:
      console.printf("  NOTE    0x%03X stops while the action is on\n", f.id);
      break;
  }
}

// Compares rest against active for every bit on the bus.
static void analyse(int stepIndex) {
  static Finding found[MAX_FINDINGS];
  int count = 0;
  bool overflow = false;

  auto add = [&](const Finding &f) {
    if (count < MAX_FINDINGS) {
      found[count++] = f;
    } else {
      overflow = true;
    }
  };

  for (int i = 0; i < seenCount; i++) {
    const Stat &s = stats[i];
    uint16_t id = (uint16_t)seenIds[i];

    if (s.restCount == 0 && s.actCount == 0 && s.anyCount == 0) continue;
    if (s.restCount == 0) {
      add({id, K_ONLY_ACTIVE});
      continue;
    }
    if (s.actCount == 0) {
      if (s.anyCount == 0) add({id, K_ONLY_REST});
      continue;
    }

    for (int b = 0; b < s.dlc; b++) {
      uint8_t restStable = ~(s.restAnd[b] ^ s.restOr[b]);
      uint8_t actStable  = ~(s.actAnd[b] ^ s.actOr[b]);
      uint8_t strong = restStable & actStable & (s.restAnd[b] ^ s.actAnd[b]);

      if (strong) {
        add({id, K_STRONG, (uint8_t)b, strong,
             (uint8_t)(s.restAnd[b] & strong), (uint8_t)(s.actAnd[b] & strong)});
      }

      bool apart = s.actMin[b] > s.restMax[b] || s.actMax[b] < s.restMin[b];
      if (!strong && apart) {
        add({id, K_ANALOG, (uint8_t)b, 0,
             s.restMin[b], s.restMax[b], s.actMin[b], s.actMax[b]});
        continue;
      }

      // Steady at rest, but differed from the rest value at some point.
      uint8_t moved = (s.anyOr[b] ^ s.restAnd[b]) | (s.anyAnd[b] ^ s.restAnd[b]);
      uint8_t pulse = restStable & moved & ~strong;
      if (pulse) {
        add({id, K_PULSE, (uint8_t)b, pulse});
      }
    }
  }

  // Store everything for the summary, best evidence first.
  StepResult &result = results[stepIndex];
  result.status = DONE;
  result.overflow = overflow;
  result.count = 0;
  static const Kind order[] = {K_STRONG, K_ANALOG, K_PULSE, K_ONLY_ACTIVE, K_ONLY_REST};
  for (Kind kind : order) {
    for (int i = 0; i < count; i++) {
      if (found[i].kind == kind) result.findings[result.count++] = found[i];
    }
  }

  // Right after the step only the first lines are shown, to keep the prompt
  // for the next step on screen. The summary at the end prints all of them.
  console.printf("\n--- Result: %s ---\n", steps[stepIndex].name);
  int shown = min((int)result.count, PRINT_LIMIT);
  for (int i = 0; i < shown; i++) {
    printFinding(result.findings[i]);
  }

  if (result.count == 0) {
    console.println("  Nothing followed this control. It is probably not on the CAN");
    console.println("  bus (wired straight to the dash), or the timing was off: b = retry.");
  } else if (result.count > shown || overflow) {
    console.printf("  ... and %d%s more, listed in the summary. That many hits usually\n",
                   result.count - shown, overflow ? "+" : "");
    console.println("  means something else changed too (engine, bike moving). b = retry.");
  }
  if (result.count > 1) {
    console.println("  Several hits in one ID are normal: a checksum or a copy of the bit.");
  }
}

// ---------- State machine ----------------------------------------------------

enum State : uint8_t {
  OFF,
  WAIT_START,  // step described, waiting for Enter
  WAIT_ENTER,  // STATE step: waiting for you to set the next state
  RUNNING,     // settling, then sampling
  SUMMARY,     // printing the final summary, a line at a time
};

static State state = OFF;
static int stepIndex = 0;
static int phase = 0;
static uint32_t phaseStartMs = 0;
static uint32_t lastInputMs = 0;

// Which sample sets the frames arriving right now belong to.
static bool sampleRest = false;
static bool sampleAct = false;
static bool sampleAny = false;

static void stopSampling() {
  sampleRest = sampleAct = sampleAny = false;
}

// The summary lists every finding of every step. That is far more text than
// the Wi-Fi buffer holds, so it goes out a line at a time. That
// rate is far below what the link carries.
static const uint32_t SUMMARY_LINE_MS = 15;
static uint32_t lastSummaryLineMs = 0;
static int summaryStep = 0;
static int summaryLine = 0;  // -1 = the step's heading

static void finish() {
  stopSampling();
  console.println();
  console.println("================ SIGNAL FINDER SUMMARY ================");
  console.println("Every finding of every step. Copy this into your notes.");
  summaryStep = 0;
  summaryLine = -1;
  state = SUMMARY;
}

static void summaryUpdate() {
  if (millis() - lastSummaryLineMs < SUMMARY_LINE_MS) return;
  lastSummaryLineMs = millis();

  if (summaryStep >= STEP_COUNT) {
    console.println("================== END OF SUMMARY =====================");
    console.println();
    console.println("Signal finder ended. Send t to run it again.");
    state = OFF;
    return;
  }

  const StepResult &result = results[summaryStep];

  if (summaryLine < 0) {
    console.printf("\n[%d/%d] %s", summaryStep + 1, STEP_COUNT, steps[summaryStep].name);
    if (result.status == NOT_RUN) {
      console.println(": not run");
    } else if (result.status == SKIPPED) {
      console.println(": skipped");
    } else if (result.count == 0) {
      console.println(": nothing found");
    } else {
      console.printf(": %d finding%s%s\n", result.count, result.count == 1 ? "" : "s",
                     result.overflow ? " (list full, there were more)" : "");
    }
    summaryLine = 0;
  } else if (result.status == DONE && summaryLine < result.count) {
    printFinding(result.findings[summaryLine++]);
    return;
  }

  if (result.status != DONE || summaryLine >= result.count) {
    summaryStep++;
    summaryLine = -1;
  }
}

static void showStep() {
  stopSampling();
  if (stepIndex >= STEP_COUNT) {
    finish();
    return;
  }

  const Step &step = steps[stepIndex];
  console.printf("\n\a=== Step %d/%d: %s ===\n", stepIndex + 1, STEP_COUNT, step.name);
  if (step.note) console.printf("%s\n", step.note);

  if (step.mode == HOLD) {
    console.printf("Timed, %d times. On HOLD: %s. On RELEASE: %s.\n",
                   step.phases / 2, step.active, step.rest);
    console.println("Start released. Do nothing else on the bike during the step.");
  } else {
    console.printf("A = %s\nB = %s\n", step.rest, step.active);
    console.println("Set A now. Do nothing else on the bike while it samples.");
  }
  console.println("Enter = start   s = skip   b = previous step   q = quit + summary");
  state = WAIT_START;
}

static void startPhase() {
  const Step &step = steps[stepIndex];
  bool active = phase & 1;

  if (step.mode == HOLD) {
    if (active) {
      console.printf("\a>>> HOLD %d/%d: %s\n", phase / 2 + 1, step.phases / 2, step.active);
    } else if (phase == 0) {
      console.println(">>> Hands off. Reading the rest state...");
    } else {
      console.printf("\a>>> RELEASE: %s\n", step.rest);
    }
  } else {
    console.printf(">>> Sampling %s, keep still...\n", active ? "B" : "A");
  }

  phaseStartMs = millis();
  state = RUNNING;
}

static void nextPhase() {
  stopSampling();
  const Step &step = steps[stepIndex];
  phase++;

  if (phase >= step.phases) {
    analyse(stepIndex);
    stepIndex++;
    showStep();
    return;
  }

  if (step.mode == HOLD) {
    startPhase();
  } else {
    console.printf("\a>>> Now set %s: %s. Then press Enter.\n",
                   (phase & 1) ? "B" : "A", (phase & 1) ? step.active : step.rest);
    state = WAIT_ENTER;
  }
}

static void beginStep() {
  if (seenCount == 0) {
    console.println("No CAN frames seen yet. Turn the ignition on, then press Enter.");
    return;
  }
  resetStats();
  phase = 0;
  startPhase();
}

static void skipStep() {
  stopSampling();
  results[stepIndex].status = SKIPPED;
  console.println("Skipped.");
  stepIndex++;
  showStep();
}

// ---------- Start, frames, input, timing -------------------------------------------------

static void finderStart() {
  for (int i = 0; i < STEP_COUNT; i++) {
    results[i] = StepResult{};
  }
  stepIndex = 0;
  lastInputMs = millis();

  console.println();
  console.println("======== SIGNAL FINDER ========");
  console.println("Finds the CAN ID, byte and bits behind each control on the bike.");
  console.println("Setup: ignition ON, engine OFF, kill switch at RUN, gearbox in");
  console.println("neutral, bike standing still. Follow the prompts exactly and touch");
  console.println("nothing else during a step: anything that changes gets reported.");
  console.printf("%d steps, about 30 s each. Skip whatever the bike does not have.\n",
                 STEP_COUNT);
  showStep();
}

static void onFrame(const twai_message_t &msg) {
  if (msg.rtr) return;  // remote frames carry no data

  int slot = -1;
  for (int i = 0; i < seenCount; i++) {
    if (seenIds[i] == msg.identifier) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    if (seenCount >= MAX_IDS) return;  // table full: this ID is not analysed
    slot = seenCount++;
    seenIds[slot] = msg.identifier;
  }

  if (!(sampleRest || sampleAct || sampleAny)) return;

  Stat &s = stats[slot];
  int length = min((int)msg.data_length_code, 8);
  if (length > s.dlc) s.dlc = length;

  if (sampleRest) {
    s.restCount++;
    for (int b = 0; b < length; b++) {
      uint8_t v = msg.data[b];
      s.restAnd[b] &= v;
      s.restOr[b] |= v;
      if (v < s.restMin[b]) s.restMin[b] = v;
      if (v > s.restMax[b]) s.restMax[b] = v;
    }
  }
  if (sampleAct) {
    s.actCount++;
    for (int b = 0; b < length; b++) {
      uint8_t v = msg.data[b];
      s.actAnd[b] &= v;
      s.actOr[b] |= v;
      if (v < s.actMin[b]) s.actMin[b] = v;
      if (v > s.actMax[b]) s.actMax[b] = v;
    }
  }
  if (sampleAny) {
    s.anyCount++;
    for (int b = 0; b < length; b++) {
      s.anyAnd[b] &= msg.data[b];
      s.anyOr[b] |= msg.data[b];
    }
  }
}

static void finderInput(int c) {
  if (state == SUMMARY || c == 0) return;
  if (state == OFF) {
    if (c == 't') finderStart();
    return;
  }

  uint32_t now = millis();
  bool enter = (c == '\r' || c == '\n');
  bool tooSoon = (now - lastInputMs) < ENTER_DEBOUNCE_MS;
  lastInputMs = now;
  if (enter && tooSoon) return;  // the "\n" of "\r\n", or the Enter after a letter

  if (c == 'q') {
    console.println("Quit.");
    finish();
    return;
  }
  if (c == 's') {
    skipStep();
    return;
  }

  if (state == WAIT_START) {
    if (enter) {
      beginStep();
    } else if (c == 'b' && stepIndex > 0) {
      stepIndex--;
      showStep();
    }
  } else if (state == WAIT_ENTER && enter) {
    startPhase();
  }
}

static void finderUpdate() {
  if (state == SUMMARY) {
    summaryUpdate();
    return;
  }
  if (state != RUNNING) return;

  const Step &step = steps[stepIndex];
  bool hold = (step.mode == HOLD);
  uint32_t settleMs = hold ? HOLD_SETTLE_MS : STATE_SETTLE_MS;
  uint32_t sampleMs = hold ? HOLD_SAMPLE_MS : STATE_SAMPLE_MS;

  uint32_t elapsed = millis() - phaseStartMs;
  bool active = phase & 1;
  bool settled = elapsed >= settleMs;

  sampleRest = !active && settled;
  sampleAct = active && settled;
  // In a HOLD step the press itself counts, so short events are not missed.
  sampleAny = active && (hold || settled);

  if (elapsed >= settleMs + sampleMs) {
    nextPhase();
  }
}

// ---------- Program ----------------------------------------------------------

static const uint32_t IDLE_HINT_MS = 5000;
static uint32_t lastHintMs = 0;

void setup() {
  consoleSetup();
  console.println("KTM 390 signal finder (test firmware), listen-only");

  // Listen-only: the controller never transmits and never sends ACK or error
  // frames. Same settings as the main program.
  twai_general_config_t generalConfig =
      TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_LISTEN_ONLY);
  generalConfig.tx_queue_len = 0;
  generalConfig.rx_queue_len = 256;
  twai_timing_config_t timingConfig = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t filterConfig = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&generalConfig, &timingConfig, &filterConfig) != ESP_OK ||
      twai_start() != ESP_OK) {
    console.println("TWAI start failed. Check the board selection and pins.");
    while (true) {
      delay(1000);
    }
  }
}

void loop() {
  twai_message_t msg;
  if (twai_receive(&msg, pdMS_TO_TICKS(2)) == ESP_OK) {
    do {
      onFrame(msg);
    } while (twai_receive(&msg, 0) == ESP_OK);
  }

  consoleUpdate();

  int c;
  while ((c = console.read()) >= 0) {
    finderInput(c);
  }
  finderUpdate();

  // The console's own greeting names the main program's commands, so keep
  // saying what this firmware wants until the test is started.
  if (state == OFF && millis() - lastHintMs >= IDLE_HINT_MS) {
    lastHintMs = millis();
    console.printf("Signal finder ready, %d CAN IDs seen. Send t to start.\n", seenCount);
  }
}
