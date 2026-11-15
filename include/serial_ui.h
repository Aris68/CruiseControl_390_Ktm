// Serial Monitor output and commands.
//   i  print every CAN ID seen. '*' marks bytes that changed since the last 'i'
//   l  turn the live RPM/speed lines on or off
//   t  guided signal finder: tells you what to press, reports ID/byte/bits
#pragma once

void uiSetup();

// Reads commands and prints the live and status lines when they are due.
// Call it every loop.
void uiUpdate();
