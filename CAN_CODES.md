# KTM 390 CAN codes (high certainty only)

Sources: `KTM390_CAN_signal_map.xlsx` and signal finder run 1.1 (`SIGNAL_FINDER_RESULTS.md`, STRONG hits only).
Multi-byte values are big-endian.

| ID    | Byte / bits  | Signal                         | Encoding                 | Notes                                             |
|-------|--------------|--------------------------------|--------------------------|---------------------------------------------------|
| 0x120 | D0–D1        | Engine speed                   | 16-bit, 1 rpm/count      | Verified                                          |
| 0x120 | D2           | Throttle grip                  | 0 closed → 254 open      | Verified. Signal finder confirms                  |
| 0x120 | D3 bit 0x08  | Kill switch                    | 0 = RUN                  |                                                   |
| 0x120 | D3 bit 0x10  | Start button                   | 1 while pressed          | Verified                                          |
| 0x128 | D2 bit 0x01  | Throttle-closed switch         | D2 = 03 closed, 02 open  | Verified. Signal finder confirms                  |
| 0x129 | D0 bits 4–7  | Gear                           | Gear number              | Verified. Signal finder confirms                  |
| 0x129 | D0 bit 0x08  | Clutch lever                   | 1 = pull                 | Verified. Signal finder: weak hit only            |
| 0x12A | D1           | Traction control               | 0x2D on, 0x1D off        | Verified. Signal finder confirms                  |
| 0x12A | D2 bit 0x02  | ABS mode                       | 1 = offroad              | New, signal finder. Same as 0x450 D4 bit 0x80     |
| 0x12A | D4 bit 0x20  | Ride mode                      | 1 = offroad              | New, signal finder. Same as 0x450 D4 bit 0x08     |
| 0x12A | D5 bit 0x10  | Traction control (copy)        | 1 = on                   | New, signal finder                                |
| 0x12D | D1–D2        | Rear wheel speed               | 16-bit ÷ some scale = km | Verified                                          |
| 0x12F | D1 bit 0x40  | Rear brake                     | 1 = applied              | New, signal finder. Cleanest rear brake signal    |
| 0x290 | D0–D1        | Front brake pressure           | 16-bit                   | Raw counts. Signal finder: D1 may be flags        |
| 0x290 | D3           | Brake light (front lever only) | 02 off, 0E on            | Definetely Wrong                                  |
| 0x450 | D4 bit 0x08  | Ride mode                      | 1 = offroad              | New, signal finder                                |
| 0x450 | D4 bit 0x80  | ABS mode                       | 1 = offroad              | New, signal finder                                |
| 0x540 | D3 bit 0x01  | Side stand                     | 1 = up                   | Verified. Signal finder confirms                  |
| 0x55A | D0–D2        | Odometer                       | 24-bit ÷ 10 = km         | Wrong                                             |
