# KTM 390 CAN codes (high certainty only)

Signals tested on the bike and matched against the dash, a switch, or a repeat test.
Source: `KTM390_CAN_signal_map.xlsx`. Multi-byte values are big-endian.

| ID    | Byte / bits  | Signal              | Encoding                         |
|-------|--------------|---------------------|----------------------------------|
| 0x120 | D0–D1        | Engine speed        | 16-bit, 1 rpm/count              |
| 0x120 | D2           | Throttle grip       | 0 closed → 253 open              |
| 0x120 | D3 bit 0x08  | Kill switch         | 1 = RUN                          |
| 0x120 | D3 bit 0x10  | Start button        | 1 while pressed                  |
| 0x128 | D2 bit 0x01  | Throttle-closed switch | D2 = 03 closed, 02 open       |
| 0x129 | D0 bits 4–7  | Gear                | Gear number                      |
| 0x129 | D0 bit 0x08  | Clutch lever        | Toggles with lever pull          |
| 0x12A | D1           | Traction control    | 0x28 on, 0x18 off                |
| 0x290 | D0–D1        | Front brake pressure | 16-bit                          |
| 0x290 | D3           | Brake light (front lever only) | 02 off, 0E on         |
| 0x540 | D3 bit 0x01  | Side stand          | 1 = up                           |
| 0x55A | D0–D2        | Odometer            | 24-bit ÷ 10 = km                 |

## Caveat

- **0x290 D3 brake light** is confirmed for the front lever only. It did not appear in the rear-brake test, so there is no high-certainty rear-brake signal yet.
