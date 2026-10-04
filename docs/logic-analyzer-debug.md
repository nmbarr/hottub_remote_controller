# Display tap: logic analyzer troubleshooting

## Symptom

No temperature in Home Assistant. With the tub running, the log repeats this
roughly every 19 ms and never shows `Frame received`:

```
[W][esp32-spa:343]: Dropped 3 partial/incomplete frames (gaps before 21 bits)
```

The ISR counts rising edges on GPIO35 as clock bits. A gap longer than
`FRAME_GAP_MS` (5 ms) ends a frame, and a frame needs at least 21 bits.
Every burst it sees is shorter than that. The symptom is the same whichever of
RJ45 pins 5/6 is on GPIO35, so swapping them didn't fix it.

Goal: find out what's actually on pins 5 and 6.

## Setup

- PulseView (sigrok) on Windows, since WSL2 can't see the analyzer without
  `usbipd`. Cheap 8-channel analyzers use the `fx2lafw` driver.
- Analyzer GND → RJ45 pin 4.
- D0 → RJ45 pin 6, D1 → RJ45 pin 5. Probe at the RJ45 breakout, not the
  ESP32 side of the dividers.
- Sample rate 12–24 MHz, capture 1 s (about 50 display cycles).
- Tub running, panel showing the water temperature, nobody touching buttons.

Make two captures: one with the ESP32 board connected and one with it
unplugged. If the edges differ between the two, the board is loading the
lines.

## What to measure

For one ~19 ms display cycle on each channel:

| Measure | VL260 (what the firmware expects) |
| --- | --- |
| Which channel toggles regularly (clock) | one steady burst per cycle |
| Rising edges per burst | 21–24 |
| Bursts per cycle | 1 |
| Gap between bursts | ~15 ms (well over 5 ms) |
| Clock frequency within a burst | note it |
| Data stable on clock rising edge? | yes (firmware samples ~1 µs after rising) |

PulseView's SPI decoder (CLK = clock channel, MOSI = data channel, CPOL 0,
CPHA 0, 8 bits) gives a quick readout of the bits. Write the bit pattern down
next to what the panel is showing.

## Reading the result

| You see | Means | Fix |
| --- | --- | --- |
| One burst of 21–24 edges, ~15 ms gap, clean edges | The protocol matches. The problem is on our side. | Check the voltage at GPIO34/35 with a meter. It needs to be about 2.5 V or more when high. The 6.8k/10k divider may be pulling a weakly driven line too low. |
| The same frame split into 2–3 bursts | Gaps inside a frame are longer than 5 ms | Set `FRAME_GAP_MS` between the longest gap inside a frame and the gap between frames |
| More than 24 edges, or a different structure | A different frame format from the VL260 | The decode in `esp32-spa.h` needs adapting to it |
| Doubled or very narrow edges | Noise or ringing | RC filter or a Schmitt buffer on the clock input |
| Clean with the board unplugged, broken with it connected | The board is loading the lines | Use higher-value divider resistors, or a buffer |

## Afterwards

- Once you know which pin is the clock, update the schematic labels and the
  wiring table in the root README to match.
- To chase the "30" the panel shows after repeated COOL presses, capture
  while pressing COOL until it appears, then decode those frames.
