/*****************************************************************************
 *                                                                           *
 * DISPLAY                                                                   *
 *                                                                           *
 ****************************************************************************/
#if IS_DISPLAY_ENABLED
String PrepareTopMessage(uint8_t switch_pos) {
  String result = weather_report[0] + " " + mode_name[switch_pos];
  if (!result.equals(previous_message_top)) {
    is_display_dirty = true;
    previous_message_top = result;
  }
  return result;
}

String PrepareBottomMessage() {
  if (current_weather_report_display >= kWeatherReportMaxLength ||
      weather_report[current_weather_report_display].length() == 0) {
    current_weather_report_display = 1;
  }
  String result = weather_report[current_weather_report_display];
  if (millis() - millis_when_bottom_row_updated > 3000) {
    millis_when_bottom_row_updated = millis();
    current_weather_report_display += 1;
    is_display_dirty = true;
  }
  return result;
}

void UpdateDisplay(String message_top, String message_bottom) {
  Serial.println(message_top);
  Serial.println(message_bottom);

  // Send the clear command in its own transaction — the OpenLCD needs
  // ~10ms to execute it before it can accept new characters.
  Wire.beginTransmission(kDisplayAddress);
  Wire.write('|'); //Put LCD into setting mode
  Wire.write('-'); //Send clear display command
  uint8_t clear_error = Wire.endTransmission();
  if (clear_error != 0) {
    Serial.print("I2C error clearing display: ");
    Serial.println(clear_error);
    return;
  }

  delay(10); // let the display finish the clear

  // Now send the actual content.
  Wire.beginTransmission(kDisplayAddress);
  Wire.print(message_top);
  unsigned int chars_printed = message_top.length();
  while (chars_printed < 16) {
    Wire.print(" ");
    chars_printed++;
  }
  Wire.print(message_bottom);

  uint8_t i2c_error = Wire.endTransmission(); //Stop I2C transmission
  if (i2c_error != 0) {
    Serial.print("I2C error on display: ");
    Serial.println(i2c_error);
  } else {
    is_display_dirty = false;
  }
  Serial.println("--------------");
}
#endif //IS_DISPLAY_ENABLED

/*****************************************************************************
 *                                                                           *
 * LEDS                                                                      *
 *                                                                           *
 ****************************************************************************/

#if IS_FASTLED_ENABLED
// Fill the under-cabinet strip (pin 25).
// The strip is wired right-to-left, so we reverse the index so that
// logical position 0 corresponds to the leftmost LED (section D).
void SetUnderCabinetLeds(CRGB color) {
  for (int i = 0; i < kNumLedsPin25; i++) {
    leds_pin25[kNumLedsPin25 - 1 - i] = color;
  }
}

// Fill the ceiling strips (pins 17 + 16) as one continuous run.
// Pin 17 is wired right-to-left (reversed here), then pin 16 continues
// left-to-right (natural order), so an animation starting at logical
// position 0 flows seamlessly from pin 17 into pin 16.
void SetCeilingLeds(CRGB color) {
  for (int i = 0; i < kNumLedsPin17; i++) {
    leds_pin17[kNumLedsPin17 - 1 - i] = color;
  }
  for (int i = 0; i < kNumLedsPin16; i++) {
    leds_pin16[i] = color;
  }
}

// Fill every LED on every strip with the same color.
void SetAllLeds(CRGB color) {
  SetUnderCabinetLeds(color);
  SetCeilingLeds(color);
}

// Shift the under-cabinet strip one position to the right (logically,
// left-to-right) and inject a new color at the leftmost end.
// Physical wiring is right-to-left, so leftmost = highest index.
void ShiftUnderCabinetRight(CRGB new_color) {
  for (int i = 0; i < kNumLedsPin25 - 1; i++) {
    leds_pin25[i] = leds_pin25[i + 1];
  }
  leds_pin25[kNumLedsPin25 - 1] = new_color;
}

// Shift the ceiling strips one position to the right (logically) and
// inject a new color at the leftmost end.  Pin 17 (reversed wiring)
// feeds into pin 16 (natural wiring) so the animation crosses over.
void ShiftCeilingRight(CRGB new_color) {
  // Shift pin 16 right (natural order, increasing index)
  for (int i = kNumLedsPin16 - 1; i > 0; i--) {
    leds_pin16[i] = leds_pin16[i - 1];
  }
  // Crossover: pin 17 rightmost logical (physical index 0) → pin 16 leftmost
  leds_pin16[0] = leds_pin17[0];
  // Shift pin 17 right in logical order (decreasing physical index)
  for (int i = 0; i < kNumLedsPin17 - 1; i++) {
    leds_pin17[i] = leds_pin17[i + 1];
  }
  // Inject at pin 17 leftmost logical (physical index N-1)
  leds_pin17[kNumLedsPin17 - 1] = new_color;
}

// Rainbow animation: shift all strips and inject the current hue.
// Called every loop() iteration when in Rainbow mode; the shift only
// happens when enough time has elapsed for one propagation step.
void UpdateRainbow() {
  unsigned long now = millis();
  int longest_run = max((int)kNumLedsPin25, (int)(kNumLedsPin17 + kNumLedsPin16));
  unsigned long shift_interval = kRainbowPropagationMs / longest_run;

  if (now - millis_of_last_rainbow_shift >= shift_interval) {
    millis_of_last_rainbow_shift = now;
    CRGB color = CHSV(rainbow_hue, 255, 255);
    ShiftUnderCabinetRight(color);
    ShiftCeilingRight(color);
    if (rainbow_auto_cycle) {
      rainbow_hue++;  // wraps naturally at 256 → 0
    }
    FastLED.show();
  }
}

// --- Twinkle helpers ---

// Write a color to a physical LED position, clamping to strip bounds.
void SetStripPixel(uint8_t strip, int pos, CRGB color) {
  switch (strip) {
    case 0:
      if (pos >= 0 && pos < kNumLedsPin25) leds_pin25[pos] = color;
      break;
    case 1:
      if (pos >= 0 && pos < kNumLedsPin17) leds_pin17[pos] = color;
      break;
    case 2:
      if (pos >= 0 && pos < kNumLedsPin16) leds_pin16[pos] = color;
      break;
  }
}

// Return the length of a strip by index.
int StripLength(uint8_t strip) {
  switch (strip) {
    case 0: return kNumLedsPin25;
    case 1: return kNumLedsPin17;
    case 2: return kNumLedsPin16;
    default: return 0;
  }
}

// Check whether a candidate position on a strip is too close to an
// active spot that still has significant lifetime remaining.
bool IsTooCloseToActiveSpot(uint8_t strip, uint8_t pos) {
  unsigned long now = millis();
  for (int i = 0; i < kTwinkleMaxActive; i++) {
    if (!twinkle_spots[i].active) continue;
    if (twinkle_spots[i].strip != strip) continue;
    // "Significant time left" = less than halfway through its lifetime.
    unsigned long age = now - twinkle_spots[i].birth_ms;
    if (age > kTwinkleSpotLifetime / 2) continue;
    // Too close if within ±2 of an active bright spot.
    int dist = abs((int)pos - (int)twinkle_spots[i].position);
    if (dist <= 2) return true;
  }
  return false;
}

// Spawn a new twinkle spot in the first available slot.
// Tries a few random positions to avoid landing on or next to a
// still-bright spot; gives up after a handful of attempts.
constexpr int kTwinkleSpawnAttempts = 5;
void SpawnTwinkleSpot() {
  for (int i = 0; i < kTwinkleMaxActive; i++) {
    if (!twinkle_spots[i].active) {
      uint8_t strip = random(3);
      uint8_t pos = random(StripLength(strip));

      // Try to find a position that isn't crowded.
      for (int attempt = 0; attempt < kTwinkleSpawnAttempts; attempt++) {
        if (!IsTooCloseToActiveSpot(strip, pos)) break;
        strip = random(3);
        pos = random(StripLength(strip));
      }
      // If we still collide after all attempts, spawn anyway — it's
      // better than visibly dropping spawns.

      twinkle_spots[i].strip = strip;
      twinkle_spots[i].position = pos;
      if (twinkle_monochrome) {
        twinkle_spots[i].hue = twinkle_mono_hue;
      } else {
        uint8_t palette_index = random(kTwinklePaletteSize);
        twinkle_spots[i].hue = twinkle_palette[palette_index];
        twinkle_mono_hue = twinkle_palette[palette_index];  // latch for mono toggle
      }
      twinkle_spots[i].birth_ms = millis();
      twinkle_spots[i].active = true;
      return;
    }
  }
  // All slots full — skip this spawn.
}

// Refresh active twinkle spots: fade in over lifetime, spread to neighbors.
void UpdateTwinkleSpots() {
  unsigned long now = millis();
  for (int i = 0; i < kTwinkleMaxActive; i++) {
    if (!twinkle_spots[i].active) continue;

    unsigned long age = now - twinkle_spots[i].birth_ms;
    if (age > kTwinkleSpotLifetime) {
      twinkle_spots[i].active = false;
      continue;
    }

    // Brightness ramps from 0 → 255 over the first half, then holds at 255.
    float fraction = (float)age / kTwinkleSpotLifetime;
    uint8_t brightness;
    if (fraction < 0.5) {
      brightness = (uint8_t)(255 * (fraction * 2.0));  // ramp up
    } else {
      brightness = 255;  // hold
    }

    CRGB color = CHSV(twinkle_spots[i].hue, 255, brightness);
    uint8_t s = twinkle_spots[i].strip;
    int p = twinkle_spots[i].position;

    // Center pixel at full computed brightness.
    SetStripPixel(s, p, color);

    // Neighbors at half brightness for a gentle spread.
    CRGB neighbor_color = CHSV(twinkle_spots[i].hue, 255, brightness / 2);
    SetStripPixel(s, p - 1, neighbor_color);
    SetStripPixel(s, p + 1, neighbor_color);
  }
}

// Main Twinkle update — called every loop() when in Twinkle mode.
void UpdateTwinkle() {
  // Global fade: every LED dims a little each frame.
  fadeToBlackBy(leds_pin25, kNumLedsPin25, kTwinkleFadeAmount);
  fadeToBlackBy(leds_pin17, kNumLedsPin17, kTwinkleFadeAmount);
  fadeToBlackBy(leds_pin16, kNumLedsPin16, kTwinkleFadeAmount);

  // Spawn new spots at the configured rate.
  unsigned long now = millis();
  if (now - millis_of_last_twinkle_spawn >= millis_between_twinkle_spawns) {
    millis_of_last_twinkle_spawn = now;
    SpawnTwinkleSpot();
  }

  // Refresh active spots (overrides the fade for living spots).
  UpdateTwinkleSpots();

  FastLED.show();
}

// --- Fire helpers ---

// Map a heat value (0–255) to a fire color.
// Warm palette: black → red → orange → yellow → white (via HeatColor).
// Cool palette: black → blue → purple → cyan → white (R and B swapped).
CRGB FireColorFromHeat(uint8_t heat, bool cool) {
  CRGB color = HeatColor(heat);
  if (cool) {
    // Swap red and blue channels for a blue-fire look.
    uint8_t tmp = color.r;
    color.r = color.b;
    color.b = tmp;
  }
  return color;
}

// Run one frame of fire simulation on a single strip's heat array,
// then write the resulting colors into the LED array.
// The heat array is indexed in "logical" order (0 = fire source end).
// If reversed is true, physical LED index is mirrored so that logical
// index 0 maps to the highest physical index (for right-to-left wiring).
void UpdateFireStrip(CRGB* leds, uint8_t* heat, int num_leds, bool reversed) {
  // Step 1: Cool each cell by a small random amount.
  for (int i = 0; i < num_leds; i++) {
    heat[i] = qsub8(heat[i], random8(0, ((kFireCooling * 10) / num_leds) + 2));
  }

  // Step 2: Drift heat "upward" (from low index toward high index).
  // Work from the top down so we don't double-count.
  for (int i = num_leds - 1; i >= 2; i--) {
    heat[i] = (heat[i - 1] + heat[i - 2] + heat[i - 2]) / 3;
  }

  // Step 3: Randomly ignite new sparks near the bottom (source end).
  if (random8() < fire_sparking) {
    int spark_pos = random8(7);  // one of the first 7 cells
    if (spark_pos < num_leds) {
      heat[spark_pos] = qadd8(heat[spark_pos], random8(160, 255));
    }
  }

  // Step 4: Map heat to color and write to the LED array.
  for (int i = 0; i < num_leds; i++) {
    int physical_index = reversed ? (num_leds - 1 - i) : i;
    leds[physical_index] = FireColorFromHeat(heat[i], fire_cool_palette);
  }
}

// Main Fire update — called every loop() when in Fire mode.
// Throttled to kFireFrameMs so the effect runs at a mellow pace.
void UpdateFire() {
  unsigned long now = millis();
  if (now - millis_of_last_fire_frame < kFireFrameMs) return;
  millis_of_last_fire_frame = now;

  // Pin 25 (under-cabinet): wired right-to-left, fire source at left (reversed).
  UpdateFireStrip(leds_pin25, heat_pin25, kNumLedsPin25, true);
  // Pin 17 (ceiling left): wired right-to-left, fire source at left (reversed).
  UpdateFireStrip(leds_pin17, heat_pin17, kNumLedsPin17, true);
  // Pin 16 (ceiling right): wired left-to-right, fire source at left (natural).
  UpdateFireStrip(leds_pin16, heat_pin16, kNumLedsPin16, false);
  FastLED.show();
}
#endif


/*****************************************************************************
 *                                                                           *
 * INPUT                                                                     *
 *                                                                           *
 ****************************************************************************/

int GetSwitchPosition() {
  uint16_t input = analogRead(kModeSwitchPin);
  uint8_t val;

  if (input < 100) {
    // position 0 always reads 0
    val = 0;
  } else if (input < 400) {
    // position 1 is about 270
    val = 1;
  } else if (input < 900) {
    // position 2 is about 710
    val = 2;
  } else if (input < 1300) {
    // position 3 is about 1150
    val = 3;
  } else if (input < 1800) {
    // position 4 is about 1610
    val = 4;
  } else if (input < 2300) {
    // position 5 is about 2050
    val = 5;
  } else if (input < 2700) {
    // position 6 is about 2500
    val = 6;
  } else if (input < 3200) {
    // position 7 is about 2950
    val = 7;
  } else if (input < 3900) {
    // position 8 is about 3650
    val = 8;
  } else {
    // position 9 is always 4095
    val = 9;
  }

  return val;
}

// Debounce the mode switch and return the current committed switch position.
// Reads the raw position and only commits a new value once it has been stable
// for 50ms, filtering out sporadic analog noise.
int GetDebouncedSwitchPosition() {
  int raw_switch_position = GetSwitchPosition();
  if (raw_switch_position != candidate_switch_position) {
    candidate_switch_position = raw_switch_position;
    millis_when_candidate_changed = millis();
  }
  int next_switch_position = committed_switch_position;
  if (candidate_switch_position != committed_switch_position &&
      millis() - millis_when_candidate_changed >= 50) {
    committed_switch_position = candidate_switch_position;
    next_switch_position = committed_switch_position;
#if IS_DISPLAY_ENABLED
    // mode just changed, and we want the display to refresh immediately.
    // to do this, pretend we have never attempted to update the display
    // before. (this variable normally prevents us from spamming the bus.)
    millis_of_last_display_attempt = 0;
#endif
#if IS_FASTLED_ENABLED
    is_led_dirty = true;
    // Animated modes bypass the normal brightness-from-twist path, so
    // if we're arriving from a presence-faded mode (brightness == 0)
    // the lights would stay dark.  Reset to full on any mode change.
    FastLED.setBrightness(kBrightness);
    previous_brightness = kBrightness;
    if (next_switch_position == kFireModeIndex) {
      // Entering Fire: zero out heat arrays and reset state.
      memset(heat_pin25, 0, sizeof(heat_pin25));
      memset(heat_pin17, 0, sizeof(heat_pin17));
      memset(heat_pin16, 0, sizeof(heat_pin16));
      fire_sparking = kFireSparking;
      fire_cool_palette = false;
#if IS_TWIST_ENABLED
      fire_twist_baseline = twist.getCount();
#endif
      SetAllLeds(CRGB::Black);
    }
    if (next_switch_position == kRainbowModeIndex) {
      // Entering Rainbow: pick a random starting hue, reset state, and
      // clear the strips so colors grow outward from the left.
      rainbow_hue = random(256);
      rainbow_auto_cycle = true;
      millis_of_last_rainbow_shift = millis();
#if IS_TWIST_ENABLED
      rainbow_twist_baseline = twist.getCount();
#endif
      SetAllLeds(CRGB::Black);
    }
    if (next_switch_position == kTwinkleModeIndex) {
      // Entering Twinkle: clear strips and reset all twinkle state.
      for (int i = 0; i < kTwinkleMaxActive; i++) {
        twinkle_spots[i].active = false;
      }
      twinkle_monochrome = false;
      millis_between_twinkle_spawns = kTwinkleDefaultSpawnMs;
      millis_of_last_twinkle_spawn = millis();
#if IS_TWIST_ENABLED
      twinkle_twist_baseline = twist.getCount();
#endif
      SetAllLeds(CRGB::Black);
    }
#endif
  }
  return next_switch_position;
}


#if IS_TWIST_ENABLED
// Clamp the twist brightness window so the current position stays inside it.
void ClampTwistWindow(int current_twist_position) {
  if (current_twist_position < twist_brightness_window_min) {
    twist_brightness_window_min = current_twist_position;
    twist_brightness_window_max = current_twist_position + kTwistBrightnessWindowSize;
  }
  if (current_twist_position > twist_brightness_window_max) {
    twist_brightness_window_min = current_twist_position - kTwistBrightnessWindowSize;
    twist_brightness_window_max = current_twist_position;
  }
}
#endif

/*****************************************************************************
 *                                                                           *
 * PRESENCE                                                                  *
 *                                                                           *
 ****************************************************************************/
#if IS_PRESENCE_ENABLED
int16_t CheckPresence() {
  // Clear the data-ready flag so the sensor will process its next
  // measurement cycle. Without this, the algorithm stalls and no
  // further presence/motion interrupts are generated.
  sths34pf80_tmos_drdy_status_t data_ready;
  presence_sensor.getDataReady(&data_ready);

  // Read the status register (clears the interrupt latch on the INT pin).
  sths34pf80_tmos_func_status_t status;
  if (presence_sensor.getStatus(&status) != 0) {
    Serial.println("I2C error reading presence status");
    return 0;
  }

  // Require motion to corroborate presence detection.
  // The presence flag alone is prone to false positives from ambient
  // temperature drift, so we only trigger when motion is also detected.
  if (status.pres_flag == 1 && status.mot_flag == 1) {
    // Presence Units: cm^-1
    if (presence_sensor.getPresenceValue(&presence_val) != 0) {
      Serial.println("I2C error reading presence value");
      return 0;
    }
    Serial.print("Presence+Motion: ");
    Serial.print(presence_val);
    Serial.println(" cm^-1");
    return presence_val;
  }

  return 0;
}

// Returns the fraction of brightness remaining during a fade-out,
// given how long ago the fade-out started. 1.0 = just started, 0.0 = fully off.
float FadeOutBrightnessRatio(unsigned long fade_out_elapsed) {
  if (fade_out_elapsed >= kFadeDurationMs) return 0.0;
  return 1.0 - (1.0 * fade_out_elapsed / kFadeDurationMs);
}

// Fade brightness in when presence is detected and out when it times out.
// timeout controls how long after the last detection before fading begins.
int ApplyPresenceFade(int brightness, unsigned long timeout) {
  unsigned long now = millis();
  unsigned long millis_since_presence = now - millis_of_last_presence_detection;

  // Phase 3: fully timed out — lights off.
  if (millis_since_presence > timeout + kFadeDurationMs) {
    return 0;
  }
  // Phase 2: fading out after timeout.
  if (millis_since_presence > timeout) {
    int brightness_reduction = (int)(255 * (1.0 - FadeOutBrightnessRatio(millis_since_presence - timeout)));
    return max(0, brightness - brightness_reduction);
  }

  // Phase 1: presence is active. Fade in if we recently came from darkness.
  unsigned long millis_since_fade_in = now - millis_of_presence_fade_in_start;
  if (millis_since_fade_in < kFadeInDurationMs) {
    float amount_fade_in_complete = 1.0 * millis_since_fade_in / kFadeInDurationMs;
    return (int)(brightness * amount_fade_in_complete);
  }

  return brightness;
}
#endif // IS_PRESENCE_ENABLED


/*****************************************************************************
 *                                                                           *
 * WEATHER                                                                   *
 *                                                                           *
 ****************************************************************************/
// Returns the LED color Routine mode should use based on the current time
// relative to sunrise and sunset. Falls back to the default Routine color
// if time data hasn't been parsed yet.
#if IS_FASTLED_ENABLED
CRGB GetRoutineColor() {
  if (current_time_hours < 0 || sunrise_hours < 0 || sunset_hours < 0) {
    return mode_color[kRoutineModeIndex];
  }
  int now     = current_time_hours * 60 + current_time_minutes;
  int sunrise = sunrise_hours     * 60 + sunrise_minutes;
  int sunset  = sunset_hours      * 60 + sunset_minutes;

  if(millis() % 5000 < 10) Serial.printf("now: %d  | sunrise: %d |  sunset: %d\n", now, sunrise, sunset);


  if (now < sunrise - 60)  return mode_color[kNightModeIndex];   // deep night
  if (now < sunrise + 30)  return mode_color[4];                  // Dishes — near sunrise
  if (now < sunset  - 60)  return mode_color[2];                  // Cook Day
  if (now < sunset)        return mode_color[3];                  // Cook Night — pre-sunset
  if (now < sunset  + 60)  return mode_color[4];                  // Dishes — post-sunset
  return mode_color[kNightModeIndex];                            // night
}
#endif // IS_FASTLED_ENABLED

#if IS_WIFI_ENABLED
void FetchWeatherReport() {
  // wait for WiFi connection
  Serial.println("about to try to use wifi");
  if ((wifi_multi.run() == WL_CONNECTED)) {

    HTTPClient http;
    http.begin(WEATHER_URL);
    Serial.print("Requesting ");
    Serial.println(WEATHER_URL);
    // start connection and send HTTP header
    int http_code = http.GET();

    // http_code will be negative on error
    millis_when_weather_last_fetched = millis();
    if (http_code > 0) {
      // HTTP header has been send and Server response header has been handled
      // file found at server
      if (http_code == HTTP_CODE_OK) {
        String payload = http.getString();
        ParseWeatherReport(payload);
        Serial.print("Weather report: ");
        Serial.print(millis_when_weather_last_fetched);
        Serial.print(" - ");
        Serial.println(payload);

      }
    } else {
      Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(http_code).c_str());
    }

    http.end();
  }
}

void ParseWeatherReport(String raw) {
  for (int i = 0; i < kWeatherReportMaxLength; ++i) {
    weather_report[i] = "";
  }

  int tokens_found = 0;
  int token_start = 0;
  for (int i = 0; i < raw.length(); ++i) {
    // expressing the degree symbol seems complex. This method seems to work for me:
    // https://forum.arduino.cc/t/solved-how-to-print-the-degree-symbol-extended-ascii/438685/40
    // but I don't yet know how to put that character into my text file. So instead,
    // in the text file on the server I'm outputting ^ character where ° should go.
    // This little check swaps the ^ for a character which appears as a degree symbol on
    // my display.
    if (raw.charAt(i) == '^') {
      raw.setCharAt(i, char(223));
    }

    // Use the | character as a delimiter to mark what info should be
    if (raw.charAt(i) == '|') {
      String token = raw.substring(token_start,i);
      i += 1;
      token_start = i;
      if (tokens_found < kWeatherReportMaxLength) {
        weather_report[tokens_found] = token;
        tokens_found += 1;
      }
    }
  }
  // Capture the final token after the last delimiter.
  if (token_start < raw.length() && tokens_found < kWeatherReportMaxLength) {
    weather_report[tokens_found] = raw.substring(token_start);
  }

  // Parse current time from weather_report[0] (format "H:MM" or "HH:MM").
  {
    int colon = weather_report[0].indexOf(':');
    if (colon > 0) {
      current_time_hours   = weather_report[0].substring(0, colon).toInt();
      current_time_minutes = weather_report[0].substring(colon + 1).toInt();
    }
  }

  // Scan all tokens for "Sunrise: ..." and "Sunset: ..." entries.
  for (int i = 1; i < kWeatherReportMaxLength; ++i) {
    if (weather_report[i].startsWith("Sunrise: ")) {
      String t = weather_report[i].substring(9);  // after "Sunrise: "
      int colon = t.indexOf(':');
      if (colon > 0) {
        sunrise_hours   = t.substring(0, colon).toInt();
        sunrise_minutes = t.substring(colon + 1).toInt();
      }
    } else if (weather_report[i].startsWith("Sunset: ")) {
      String t = weather_report[i].substring(8);  // after "Sunset: "
      int colon = t.indexOf(':');
      if (colon > 0) {
        sunset_hours   = t.substring(0, colon).toInt();
        sunset_minutes = t.substring(colon + 1).toInt();
      }
    }
  }
}
#endif // IS_WIFI_ENABLED

/*****************************************************************************
 *                                                                           *
 * AIR QUALITY                                                               *
 *                                                                           *
 ****************************************************************************/

#if IS_AIR_SENSOR_ENABLED && IS_WIFI_ENABLED
void ReportAirQuality() {

  // some floats to store read values in
  float ambient_humidity41;
  float ambient_temperature41;
  float co2;
  float mass_concentration_pm1p0;
  float mass_concentration_pm2p5;
  float mass_concentration_pm4p0;
  float mass_concentration_pm10p0;
  float ambient_humidity55;
  float ambient_temperature55;
  float voc_index;
  float nox_index;

  // readMeasurement will return true when fresh data is available
  if (sen41.readMeasurement()) {
    ambient_humidity41 = sen41.getHumidity();
    ambient_temperature41 = sen41.getTemperature();
    co2 = sen41.getCO2();
  } else {
    Serial.print(F("."));
  }
  uint16_t error;
  char error_message[256];

  error = sen55.readMeasuredValues(
      mass_concentration_pm1p0, mass_concentration_pm2p5, mass_concentration_pm4p0,
      mass_concentration_pm10p0, ambient_humidity55, ambient_temperature55, voc_index,
      nox_index);

  if (error) {
      Serial.print("Error trying to execute readMeasuredValues(): ");
      errorToString(error, error_message, 256);
      Serial.println(error_message);
  }

  String air_url = AIR_URL;
  air_url += TEMP_41_PREFIX;
  air_url += ambient_temperature41;
  air_url += TEMP_55_PREFIX;
  air_url += ambient_temperature55;
  air_url += CO2_PREFIX;
  air_url += co2;
  air_url += HUMIDITY_41_PREFIX;
  air_url += ambient_humidity41;
  air_url += HUMIDITY_55_PREFIX;
  air_url += ambient_humidity55;
  air_url += PARTICULATE_1p0_PREFIX;
  air_url += mass_concentration_pm1p0;
  air_url += PARTICULATE_2p5_PREFIX;
  air_url += mass_concentration_pm2p5;
  air_url += PARTICULATE_4p0_PREFIX;
  air_url += mass_concentration_pm4p0;
  air_url += PARTICULATE_10_PREFIX;
  air_url += mass_concentration_pm10p0;
  air_url += VOC_PREFIX;
  air_url += voc_index;
  air_url += NOX_PREFIX;
  air_url += nox_index;
  Serial.println(air_url);
  SendAirReport(air_url);
}

void SendAirReport(String air_url) {
  // wait for WiFi connection
  if ((wifi_multi.run() == WL_CONNECTED)) {

    HTTPClient http;
    http.begin(air_url);
    // start connection and send HTTP header
    int http_code = http.GET();

    // http_code will be negative on error
    if (http_code > 0) {
      // HTTP header has been send and Server response header has been handled
      // file found at server
      if (http_code == HTTP_CODE_OK) {
        millis_when_air_last_reported = millis();
        String payload = http.getString();
        Serial.println(payload);
      }
    } else {
      Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(http_code).c_str());
    }

    http.end();
  }
}
#else
// if the air sensor is enabled, but the wi-fi is not, we would like to pretend to send
// an air report. This should compile, but does not need to do anything. See the beginning
// of loop().
inline void ReportAirQuality(){}
#endif // IS_AIR_SENSOR_ENABLED && IS_WIFI_ENABLED
