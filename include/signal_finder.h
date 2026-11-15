// Signal finder: a guided test that tells you what to press on the bike and
// works out which CAN ID, byte and bits follow that control.
//
// It runs on the console, so it works the same over telnet (Wi-Fi) and USB.
// Start it with the command 't'. It is listen-only like everything else here.
//
// How a step works:
//   The control is alternated between a rest state and an active state
//   (released / held, OFF / ON, mode A / mode B). Every frame is sorted into
//   "rest" or "active" samples, leaving out the moments right after each
//   change. Then every bit of every byte of every ID is checked:
//
//   STRONG  the bit never moved during rest, never moved during active, and
//           has a different value in the two. This is the signal, or a
//           checksum/copy that follows it.
//   ANALOG  the byte's value range during active does not overlap its range
//           during rest (pressure, throttle position, ...).
//   PULSE   the bit is steady at rest and moved at some point during the
//           action (a button that only sends a short event, a blinker).
#pragma once

#include "driver/twai.h"

// Starts the guided test at the first step.
void finderStart();

// True while the test owns the console (live output should stay quiet).
bool finderActive();

// Called for every received data frame. slot is the frame's index in idTable.
void finderOnFrame(int slot, const twai_message_t &msg);

// One typed character while the test is active.
void finderInput(int c);

// Advances the timed phases. Call it every loop.
void finderUpdate();
