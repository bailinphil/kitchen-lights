/*****************************************************************************
 *                                                                           *
 * MAIN LOOP                                                                 *
 *                                                                           *
 ****************************************************************************/

void loop(){
  delay(5);
  bool is_air_report_just_sent = false;
#if IS_AIR_SENSOR_ENABLED
  unsigned long millis_since_air_report = millis() - millis_when_air_last_reported;
  // get a new air report every every ~five minutes, but a prime number of millis
  // so that we don't try to fetch a weather report and report the air quality
  // on the same time through loop.
  if (millis_since_air_report > 299993) {
    ReportAirQuality();
    is_air_report_just_sent = true;
    millis_when_air_last_reported = millis();
  }
#endif // IS_AIR_SENSOR_ENABLED

#if IS_WIFI_ENABLED
  // updated weather info
  unsigned long millis_since_weather_fetch = millis() - millis_when_weather_last_fetched;
  // fetch weather every 30s (because it also updates the time seen on the display)
  if ((false == is_air_report_just_sent) && millis_since_weather_fetch > 30000) {
    FetchWeatherReport();
  }
#endif // IS_WIFI_ENABLED

#if IS_TWIST_ENABLED
  if (twist.isClicked()) {
#if IS_FASTLED_ENABLED
    if (committed_switch_position == kFireModeIndex) {
      // In Fire mode, the button toggles warm/cool palette.
      fire_cool_palette = !fire_cool_palette;
    } else if (committed_switch_position == kRainbowModeIndex) {
      // In Rainbow mode, the button toggles auto-cycle on/off.
      rainbow_auto_cycle = !rainbow_auto_cycle;
    } else if (committed_switch_position == kTwinkleModeIndex) {
      // In Twinkle mode, the button toggles monochrome on/off.
      twinkle_monochrome = !twinkle_monochrome;
      if (twinkle_monochrome) {
        // Latch the most recently used palette hue.
        twinkle_mono_hue = twinkle_palette[random(kTwinklePaletteSize)];
      }
    } else
#endif // IS_FASTLED_ENABLED
    {
      twist_brightness_window_center = twist.getCount();
      twist_brightness_window_min = twist.getCount() - kTwistBrightnessWindowSize;
      twist_brightness_window_max = twist.getCount() + kTwistBrightnessWindowSize;
    }
  }
#endif

  // figure out what position the switch is in, while ignoring noise
  int next_switch_position = GetDebouncedSwitchPosition();

#if IS_PRESENCE_ENABLED
  int detected_presence = 0;
  if (presence_data_ready) {
    presence_data_ready = false;
    detected_presence = CheckPresence();
  }
  if (detected_presence != 0) {
    Serial.println("Presence interrupt fired, motion and presence detected.");
    // If lights were off or fading out, start a fade-in.
    // If mid-fade-out, backdate the fade-in start so it resumes from current
    // brightness instead of snapping to darkness first.
    unsigned long millis_since_presence = millis() - millis_of_last_presence_detection;
    if (millis_since_presence > kPresenceTimeoutMs) {
      float brightness_remaining = FadeOutBrightnessRatio(millis_since_presence - kPresenceTimeoutMs);
      unsigned long fade_in_already_done = (unsigned long)(brightness_remaining * kFadeInDurationMs);
      millis_of_presence_fade_in_start = millis() - fade_in_already_done;
    }
    millis_of_last_presence_detection = millis();
  }
#endif

#if IS_TWIST_ENABLED
  delay(1); // brief gap between I2C devices to reduce bus contention
  int current_twist_position = twist.getCount();

#if IS_FASTLED_ENABLED
  if (next_switch_position == kFireModeIndex) {
    // In Fire mode the twist controls sparking intensity.
    int twist_delta = current_twist_position - fire_twist_baseline;
    int new_sparking = (int)fire_sparking + (twist_delta * 5);
    fire_sparking = constrain(new_sparking, kFireMinSparking, kFireMaxSparking);
    fire_twist_baseline = current_twist_position;
  } else if (next_switch_position == kRainbowModeIndex) {
    // In Rainbow mode the twist controls hue, not brightness.
    int twist_delta = current_twist_position - rainbow_twist_baseline;
    rainbow_hue += (uint8_t)twist_delta;  // wraps naturally
    rainbow_twist_baseline = current_twist_position;
  } else if (next_switch_position == kTwinkleModeIndex) {
    // In Twinkle mode the twist controls spawn rate.
    int twist_delta = current_twist_position - twinkle_twist_baseline;
    // Each tick adjusts spawn interval by ~10ms.
    int new_interval = (int)millis_between_twinkle_spawns - (twist_delta * 10);
    millis_between_twinkle_spawns = constrain(new_interval, kTwinkleMinSpawnMs, kTwinkleMaxSpawnMs);
    twinkle_twist_baseline = current_twist_position;
  } else
#endif // IS_FASTLED_ENABLED
  {
    // Normal modes: twist controls brightness.
    ClampTwistWindow(current_twist_position);
    float twisted_position_in_window = (current_twist_position - twist_brightness_window_min) / (1.0 * kTwistBrightnessWindowSize);
    int requested_brightness = (int)(255 * twisted_position_in_window);

#if IS_PRESENCE_ENABLED && IS_FASTLED_ENABLED
    // Fade brightness based on presence detection.
    if (next_switch_position == kNightModeIndex) {
      requested_brightness = ApplyPresenceFade(requested_brightness);
    } else if (next_switch_position == kRoutineModeIndex) {
      requested_brightness = ApplyPresenceFade(requested_brightness);
    }
#endif // IS_PRESENCE_ENABLED && IS_FASTLED_ENABLED

#if IS_FASTLED_ENABLED
    if(requested_brightness != previous_brightness){
      FastLED.setBrightness(requested_brightness);
      previous_brightness = requested_brightness;
      is_led_dirty = true;
    }
#endif // IS_FASTLED_ENABLED
  }
  uint8_t* tc = twist_colors[next_switch_position];
  twist.setColor(tc[0],tc[1],tc[2]);
#endif // IS_TWIST_ENABLED

#if IS_DISPLAY_ENABLED
  message_top = PrepareTopMessage(next_switch_position);
  message_bottom = PrepareBottomMessage();
  if (is_display_dirty && millis() - millis_of_last_display_attempt > kDisplayRetryInterval) {
    millis_of_last_display_attempt = millis();
    UpdateDisplay(message_top, message_bottom);
  }
#endif // IS_DISPLAY_ENABLED

#if IS_FASTLED_ENABLED
  if (next_switch_position == kFireModeIndex) {
    UpdateFire();
  } else if (next_switch_position == kRainbowModeIndex) {
    UpdateRainbow();
  } else if (next_switch_position == kTwinkleModeIndex) {
    UpdateTwinkle();
  } else if (next_switch_position == kRoutineModeIndex) {
    CRGB routine_color = GetRoutineColor();
    if (is_led_dirty || routine_color != previous_color) {
      SetAllLeds(routine_color);
      FastLED.show();
      previous_color = routine_color;
      is_led_dirty = false;
    }
  } else if (is_led_dirty) {
    SetAllLeds(mode_color[next_switch_position]);
    FastLED.show();
    is_led_dirty = false;
  }
#endif // IS_FASTLED_ENABLED
}
