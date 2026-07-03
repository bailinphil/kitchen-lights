# Board Outputs — Label Reference

The IoT ESP32 RedBoard drives its LED data lines through the signal amplifier
box, which exposes **three separate outputs**. Each output is a distinct logical
strip in firmware, driven from a specific GPIO and sized to a specific LED count.
**A strip only lights as many LEDs as its output is configured for** — plugging a
longer strip into a ceiling output makes it appear to "cut off" at 50.

Source of truth for pins/counts: `src/lights/lights.ino` (the `k*` constants near
the top of the FastLED section). Keep this table in sync if those change.

| Output / label | GPIO | Firmware constant(s)                                            | LED count | Physical wiring |
|----------------|------|----------------------------------------------------------------|-----------|-----------------|
| Under-cabinet  | 25   | `kNumLedsPin25` = sum of the segment constants (`kUnderCabRight`, `kSinkRight`, `kOverSink`, `kSinkLeft`, `kUnderCabCorner`, `kStove`, `kUnderCabLeft`) | full run (148 as of this writing) | wired right-to-left; logical addressing left-to-right (D, C, B, A) |
| Ceiling left   | 17   | `kCeilingLeft` (`kNumLedsPin17`)                               | 50        | wired right-to-left (reversed in software) |
| Ceiling right  | 16   | `kCeilingRight` (`kNumLedsPin16`)                              | 50        | wired left-to-right (natural order) |

## Debugging note

If a strip lights only partway and stops at a clean round number (e.g. exactly
50), suspect **wrong output** before suspecting the strip, wiring, power, or the
FastLED driver: it's almost certainly plugged into an output whose configured LED
count is shorter than the strip. Label the three output wires to avoid this.
