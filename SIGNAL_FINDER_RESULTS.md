# KTM 390 signal finder results (run 1.1)

Source: `canTest1.1.txt`. Conditions: ignition ON, engine OFF, neutral, standing still. 17 CAN IDs on the bus.

**Kind:** STRONG = clean rest → active value. PULSE = steady at rest, moves during the action. ANALOG = value range.

**vs CAN_CODES.md:** ✅ matches a verified code · 🆕 not in CAN_CODES.md · ⚠️ doubtful

## Findings

| Step | Control | ID | Byte | Mask | Kind | Rest → Active | vs CAN_CODES.md | Notes |
|---|---|---|---|---|---|---|---|---|
| 1 | Menu UP | 0x12F | D1 | 0x20 | PULSE | moves | ⚠️ | Same bit also moved during the throttle step. Probably not UP. |
| 2 | Menu DOWN | — | — | — | — | nothing found | — | Retry. |
| 3 | Menu SET | 0x540 | D6 | 0x03 | PULSE | moves | ⚠️ | Same signal as RETURN. Probably "menu active". |
| 4 | Menu RETURN | 0x540 | D6 | 0x03 | PULSE | moves | ⚠️ | Same signal as SET. |
| 5 | High beam | — | — | — | — | nothing found | — | Probably wired, not on CAN. |
| 6 | Flasher | — | — | — | — | nothing found | — | Probably wired, not on CAN. |
| 7 | Left turn signal | — | — | — | — | nothing found | — | Probably wired, not on CAN. |
| 8 | Right turn signal | — | — | — | — | nothing found | — | Probably wired, not on CAN. |
| 9 | Hazard lights | — | — | — | — | skipped | — | |
| 10 | Horn | — | — | — | — | skipped | — | |
| 11 | Front brake | 0x290 | D0 | — | ANALOG | 0 → 8..23 | ✅ | Pressure. |
| 11 | Front brake | 0x290 | D1 | 0x80 | PULSE | moves | ⚠️ | CAN_CODES.md says D0–D1 is 16-bit pressure. This bit may be a front brake flag instead. |
| 12 | Rear brake | 0x12F | D1 | 0x40 | STRONG | 0x00 → 0x40 | 🆕 | Cleanest rear brake signal. |
| 12 | Rear brake | 0x290 | D1 | 0x40 | PULSE | moves | 🆕 | Moved with no front brake applied, so D1 is likely flags, not pressure. |
| 12 | Rear brake | 0x176 | D5 | 0x03 | PULSE | moves | 🆕 | Unconfirmed. |
| 12 | Rear brake | 0x177 | D5 | 0x02 | PULSE | moves | 🆕 | Unconfirmed. |
| 13 | Clutch | 0x129 | D0 | 0x08 | PULSE | moves | ✅ | Weak hit (PULSE, not STRONG). |
| 14 | Throttle | 0x120 | D2 | 0x80 | STRONG | 0x00 → 0x80 | ✅ | Top bit of the throttle byte. |
| 14 | Throttle | 0x120 | D2 | 0x7F | PULSE | moves | ✅ | Rest of the throttle byte. |
| 14 | Throttle | 0x128 | D2 | 0x01 | STRONG | 0x01 → 0x00 | ✅ | Throttle-closed switch, 1 = closed. |
| 14 | Throttle | 0x12F | D1 | 0x20 | PULSE | moves | ⚠️ | Same bit as Menu UP. |
| 15 | Side stand (A down, B up) | 0x540 | D3 | 0x01 | STRONG | 0x00 → 0x01 | ✅ | 1 = up. |
| 15 | Side stand | 0x121 | D0 | 0xFF | STRONG | 0xFF → 0x00 | ⚠️ | Also flips with gear. Combined state? |
| 15 | Side stand | 0x121 | D1 | 0x76 | STRONG | 0x56 → 0x20 | ⚠️ | Also flips with gear. |
| 16 | Gear (A neutral, B 1st) | 0x129 | D0 | 0x10 | STRONG | 0x00 → 0x10 | ✅ | Gear number in bits 4–7. |
| 16 | Gear | 0x121 | D0 | 0xFF | STRONG | 0x00 → 0xFF | ⚠️ | See step 15. |
| 16 | Gear | 0x121 | D1 | 0x76 | STRONG | 0x20 → 0x56 | ⚠️ | See step 15. |
| 17 | MTC (A on, B off) | 0x12A | D1 | 0x30 | STRONG | 0x20 → 0x10 | ✅ | Matches 0x2D on / 0x1D off. |
| 17 | MTC | 0x12A | D5 | 0x10 | STRONG | 0x10 → 0x00 | 🆕 | Second copy, 1 = on. |
| 18 | ABS mode (A road, B offroad) | 0x450 | D4 | 0x80 | STRONG | 0x00 → 0x80 | 🆕 | 1 = offroad. |
| 18 | ABS mode | 0x12A | D2 | 0x02 | STRONG | 0x00 → 0x02 | 🆕 | 1 = offroad. |
| 19 | Ride mode Street/Rain | — | — | — | — | skipped | — | |
| 20 | Ride mode (A street, B offroad) | 0x450 | D4 | 0x08 | STRONG | 0x00 → 0x08 | 🆕 | 1 = offroad. |
| 20 | Ride mode | 0x12A | D4 | 0x20 | STRONG | 0x00 → 0x20 | 🆕 | 1 = offroad. |
| 21 | Quickshifter | — | — | — | — | skipped | — | |
| 22 | Heated grips | — | — | — | — | skipped | — | |
| 23 | Engine running | — | — | — | — | not run | — | |

## Verified codes not covered by this run

| ID | Byte / bits | Signal | Status in CAN_CODES.md |
|---|---|---|---|
| 0x120 | D0–D1 | Engine speed | Verified |
| 0x120 | D3 bit 0x08 | Kill switch | Not marked verified |
| 0x120 | D3 bit 0x10 | Start button | Verified |
| 0x12D | D1–D2 | Rear wheel speed | Verified |
| 0x290 | D3 | Brake light | Definitely wrong. Not flagged by the finder either. |
| 0x55A | D0–D2 | Odometer | Wrong |

## Still to do

- Retry Menu DOWN, and the clutch for a STRONG hit.
- Run step 23 (engine running).
- Log raw `0x290 D0` and `D1` while squeezing the front brake slowly. Smooth counting in D1 means 16-bit pressure; single flipping bits mean flags.
- Test side stand × gear together to decode `0x121`.
- Repeat the rear brake (`0x12F D1 bit 0x40`) once more, then add it to CAN_CODES.md.
