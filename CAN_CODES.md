# KTM 390 CAN codes (high certainty only)

Source: `KTM390_CAN_signal_map.xlsx`. Multi-byte values are big-endian.

| ID    | Byte / bits  | Signal                         | Encoding                | Notes                                       |
|-------|--------------|--------------------------------|-------------------------|---------------------------------------------|
| 0x120 | D0–D1        | Engine speed                   | 16-bit, 1 rpm/count     |Verified                                     |
| 0x120 | D2           | Throttle grip                  | 0 closed → 254 open     |Verified                                     |
| 0x120 | D3 bit 0x08  | Kill switch                    | 0 = RUN                 |                                             |
| 0x120 | D3 bit 0x10  | Start button                   | 1 while pressed         |Verified                                     |
| 0x128 | D2 bit 0x01  | Throttle-closed switch         | D2 = 03 closed, 02 open |Verified                                     |
| 0x129 | D0 bits 4–7  | Gear                           | Gear number             |Verified                                     |
| 0x129 | D0 bit 0x08  | Clutch lever                   | 1 = pull                |Verified                                     |
| 0x12A | D1           | Traction control               | 0x2D on, 0x1D off       |Verified                                     |
| 0x290 | D0–D1        | Front brake pressure           | 16-bit                  |Raw counts, no unit scaling yet              |
| 0x290 | D3           | Brake light (front lever only) | 02 off, 0E on           |Definetely Wrong                             |
| 0x540 | D3 bit 0x01  | Side stand                     | 1 = up                  |Verified                                     |
| 0x55A | D0–D2        | Odometer                       | 24-bit ÷ 10 = km        |            Wrong                            |
