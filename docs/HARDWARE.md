# Waveshare hardware mapping

| Function | GPIO / value |
| --- | ---: |
| CO5300 QSPI SDIO0 | 4 |
| CO5300 QSPI SDIO1 | 5 |
| CO5300 QSPI SDIO2 | 6 |
| CO5300 QSPI SDIO3 | 7 |
| CO5300 reset | 8 |
| CO5300 QSPI clock | 11 |
| CO5300 chip select | 12 |
| Panel size | 410 × 502 |
| CO5300 column offset | 22 |
| Touch SDA | 15 |
| Touch SCL | 14 |
| Touch interrupt | 38 |
| Touch reset | 9 |
| Touch I2C address | `0x38` |
| Upper physical key | BOOT / GPIO0, active low |
| Lower physical key | PWR / AXP2101 PWRON, short-press status via AXP2101 I2C `0x34` |

The panel is initialized in its native portrait orientation. The implementation
uses the same display constructor and offsets as Waveshare's public Arduino
examples. Touch is polled using the FT3x68 register layout implemented by the
FT3168 controller on the board.

The BOOT key is read directly from GPIO0. The PWR key is connected to the
AXP2101 power-management IC rather than to a spare ESP32 GPIO; its short-press
event is read from AXP2101 interrupt-status register `0x49`, bit 3, over the
shared touch I2C bus.
