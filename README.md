# Kitchen Lights

This code is the firmware, wiring, and fabrication files for a custom LED lighting system built into my kitchen. Addressable LED strips run under the cabinets and across the ceiling, driven by a microcontroller that also reads the room's temperature, air quality, and human presence, pulls live weather off the internet, and shows it all on a small display. Physical knobs switch between lighting modes and tune the current one.

## Motivation

I wanted kitchen lighting that did more than turn on and off:

- **Light quality**: I had been really annoyed by the lack of light on the counters while working, and the harshness of the light while doing the dishes at night. I wanted to be able to put the right color of light were I wanted it, without casting harsh shadows having a bright point source in my face.
- **Presence Sensing**: I wanted the lights to automatically turn off when not in use.
- **An ambient dashboard.** Since the controller was already on the counter, it became a place to surface useful info at a glance — upcoming weather for the next day in just a few lines.
- **Indoor air quality monitor**: I am curious about the level of particulate pollution and CO<sub>2</sub> in our kitchen as we cook there.
- **A hardware learning project.** It's an excuse to learn or practice embedded work: I²C device chains, soldering, enclosure design, and writing firmware that wants to run for weeks without freezing.

## What it does

A 10-position knob selects the mode; the SparkFun Qwiic Twist encoder adjusts the
active mode (brightness, speed, palette). Modes:

| Mode | Purpose |
|------|---------|
| Standby | Lights off |
| Routine | Time-of-day automatic lighting, with presence-based fade |
| Cook Day | Bright neutral task lighting |
| Cook Night | Warmer task lighting for the evening |
| Dishes | Warm cleanup lighting (used near sunrise/sunset) |
| Night | Dim red, dark-adaptation friendly; comes up on presence |
| Fire | Heat-map fire simulation |
| Rainbow | Propagating rainbow, auto-cycling or knob-driven |
| Twinkle | Blue-green sparkles that spawn and fade |
| Away | Minimal / off for when nobody's home |

The 16×2 display shows the current mode plus a rotating readout of indoor
environmental sensors and the weather forecast.

## Hardware

- **[SparkFun ESP32 IoT RedBoard](https://www.sparkfun.com/sparkfun-iot-redboard-esp32-development-board.html)** as a controller.
- **[WS2811 addressable LED strips](https://www.btf-lighting.com/products/1-ws2811-led-pixels-strip-addressable-dc12v?)** (BRG color order), split into three logical
  runs driven from separate GPIOs through a signal-amplifier box:
  
  - Under-cabinet run (GPIO 25), segmented per cabinet/sink/stove section.
  - Ceiling left (GPIO 17) and ceiling right (GPIO 16), addressed as one strip.
  
  See [wiring/outputs.md](wiring/outputs.md) for the pin/count reference.
  
  I also needed some [amplifiers](https://www.btf-lighting.com/products/sp902e-spi-signal-amplifier-repeater-2-channels-ws2812b-ws2811-sk6812-ws2814-rgbw-rgb-pixel-addressable-matrix-panel-light-5-24v) to send the signal through more than a few centimeters of wire
- **[SparkFun SerLCD / OpenLCD](https://www.sparkfun.com/sparkfun-16x2-serlcd-rgb-text-qwiic.html)** 16×2 display (I²C).
- **[SparkFun Qwiic Twist](https://www.sparkfun.com/sparkfun-qwiic-twist-rgb-rotary-encoder-breakout.html)** RGB rotary encoder (I²C) for in-mode adjustment.
- **[10-position rotary switch](https://www.sparkfun.com/products/9939)** on analog pin A5 for mode selection. ([breakout](https://www.sparkfun.com/sparkfun-rotary-switch-potentiometer-breakout.html) to turn it into a 10-position potentiometer)
- **[SparkFun STHS34PF80](https://www.sparkfun.com/sparkfun-mini-human-presence-and-motion-sensor-sths34pf80-qwiic.html)** human presence & motion sensor, on a dedicated I²C bus
  (Wire1, GPIO 4/13), interrupt-driven.
- **[SEN-25200 indoor air quality sensor](https://www.sparkfun.com/sparkfun-indoor-air-quality-combo-sensor-scd41-sen55-qwiic.html)** (temperature, humidity, CO₂,
  particulates, VOC/NOx).
- **[12 V power supply](https://www.amazon.com/gp/product/B07YYC1KM6/ref=ox_sc_act_title_1?smid=A2DYIB4IPW7T3M&th=1)** for the LED runs.

Every subsystem can be compiled out via an `IS_*_ENABLED` flag at the top of [src/lights/lights.ino](src/lights/lights.ino), so the firmware builds and runs with any subset of the hardware attached — handy for bench testing.

## Process

Roughly the path the project took:

1. **Prototype on the bench.** Get the ESP32 talking to a display, the mode
   knob, and the Twist encoder; drive a few LEDs.
2. **Get online.** Join Wi-Fi, fetch UTC time, then pull and parse a weather
   forecast for display.
3. **Add sensing.** Bring the presence sensor and the air-quality sensor onto the
   I²C chain and surface their readings.
4. **Build the real modes.** Replace demo effects with the task-oriented lighting
   modes, time-of-day automation, and presence-driven fades.
5. **Harden for uptime.** Chase down flicker, display freezes after hours of
   runtime, and I²C bus instability; move the presence sensor to its own bus and
   push all network I/O onto a dedicated FreeRTOS task so a slow request can't
   stall the render loop.
6. **Fabricate and install.** Design a mounting board for the electronics, an
   enclosure/control box, and under-cabinet mounting shields; measure and set the
   real per-segment LED counts for the installed strips.

## Repository layout

- [`src/lights/`](src/lights) — the main Arduino firmware (`lights.ino` plus
  `loop.ino`, `loop_helpers.ino`, `network.ino`, and library headers).
- [`src/Weather-Updater/`](src/Weather-Updater) — a small Python script that runs
  on my web server to fetch and stage weather data the firmware reads.
- [`src/`](src) — vendored SparkFun libraries and earlier prototype/reference
  sketches.
- [`wiring/`](wiring) — KiCad schematic/PCB for the wiring, plus
  [`outputs.md`](wiring/outputs.md).
- [`materials/`](materials) — datasheets, reference PDFs, and SVG cut/layout files
  for the enclosure, component backing board, and under-cabinet shields.
- [`display-design.txt`](display-design.txt) — layout notes for the 16×2 display.

## Tools & information sources

- **Arduino IDE** with the **FastLED** library for the LED effects.
- Vector fabrication files (SVG) for the enclosure and mounting hardware.
- **SparkFun** boards, Qwiic modules, and their example code and libraries
  (Twist, SerLCD, STHS34PF80, HTTPClient), which the firmware derives from — see
  [`src/lights/licenses.h`](src/lights/licenses.h) for attributions.
- **[OpenWeatherMap](https://openweathermap.org/)** for the weather forecast feed.
- Firmware written to follow the Google C++ Style Guide.

## Timeline

Ongoing personal project, worked on in bursts since January 2025:

1. **Jan 2025** — Initial bench prototype: ESP32 RedBoard driving a display, mode potentiometer, and Twist encoder.
2. **Feb 2025** — Wi-Fi and time; fetching, parsing, and displaying weather.
3. **Dec 2025** — Human presence sensor and the indoor air-quality sensor added; sliding-window brightness control.
4. **Mar 2026** — Major firmware work: task-oriented lighting modes (Fire, Rainbow, Twinkle, Routine), multi-region LED layout, compile-time hardware flags, a Google-style refactor, and a wave of reliability fixes.
5. **Apr 2026** — Presence sensor moved to its own interrupt-driven I²C bus; enclosure and under-cabinet shield design; firmware split for navigability.
6. **Jul 2026** — Real installed LED segment counts dialed in; all network I/O moved to a dedicated FreeRTOS task.
7. **Aug 2026** — Begin actual installation in my kitchen, completing nearly all of the under-cabinet run. 

## License

Firmware in this repository is provided under the MIT license (see [`LICENSE`](LICENSE)). Vendored third-party libraries retain their own licenses; see [`src/lights/licenses.h`](src/lights/licenses.h) and the individual library directories.
