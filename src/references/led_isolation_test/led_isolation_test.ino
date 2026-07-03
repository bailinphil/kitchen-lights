// Minimal FastLED-only test, no WiFi/sensors/modes, to isolate whether the
// "LEDs stop lighting partway through the strip" bug lives in FastLED/the
// RMT driver or in the main lights.ino application logic.
//
// Wire the strip under test to pin 25 (same pin lights.ino uses for the
// under-cabinet/over-sink run) and set kNumLeds below to match what you're
// testing:
//   - 55  to test just the over-sink segment alone
//   - 124 to test the full combined under-cabinet run
//
#include <FastLED.h>

#define CHIPSET     WS2811
#define COLOR_ORDER BRG
constexpr int kDataPin = 25;
constexpr int kNumLeds = 255;
constexpr int kBrightness = 20;

CRGB leds[kNumLeds];

void setup() {
  Serial.begin(115200);
  delay(3000);  // power-up safety delay, same as SetupLeds() in lights.ino

  FastLED.addLeds<CHIPSET, kDataPin, COLOR_ORDER>(leds, kNumLeds);
  FastLED.setBrightness(kBrightness);

  Serial.print("Isolation test: ");
  Serial.print(kNumLeds);
  Serial.println(" LEDs, solid fill.");
}

void loop() {
  // Solid white so it's obvious exactly where lighting stops.
  fill_solid(leds, kNumLeds, CRGB::White);
  FastLED.show();
  delay(500);
}
