/*****************************************************************************
 *                                                                           *
 * NETWORK                                                                   *
 *                                                                           *
 ****************************************************************************/
// All outbound network I/O runs here, on a dedicated FreeRTOS task pinned to
// core 0 (created in SetupWifi()). The Arduino render loop runs on core 1, so
// a slow or failing request in this file can never stall rendering or input.
//
// Shared state (weather_report[] and the parsed time/sunrise/sunset ints) is
// written here and read by the render loop, so every write is done under
// weather_state_mutex via Lock/UnlockWeatherState().

#if IS_WIFI_ENABLED

// Bound each request so one hung connection can't monopolize the task. These
// are generous enough for a healthy fetch (<100ms) but short enough that a
// brownout / refused connection fails fast and we retry on the normal cadence.
constexpr int32_t  kHttpConnectTimeoutMs = 1500;
constexpr uint16_t kHttpTimeoutMs        = 1500;

void FetchWeatherReport() {
  // Record the attempt time up front so the retry cadence stays fixed at ~30s
  // even when WiFi is down or the request fails. (Previously this was only
  // updated on success, so during an outage it retried every loop iteration.)
  millis_when_weather_last_fetched = millis();

  Serial.println("about to try to use wifi");
  if ((wifi_multi.run() == WL_CONNECTED)) {

    HTTPClient http;
    http.begin(WEATHER_URL);
    http.setConnectTimeout(kHttpConnectTimeoutMs);
    http.setTimeout(kHttpTimeoutMs);
    Serial.print("Requesting ");
    Serial.println(WEATHER_URL);
    // start connection and send HTTP header
    int http_code = http.GET();

    // http_code will be negative on error
    if (http_code > 0) {
      // HTTP header has been send and Server response header has been handled
      // file found at server
      if (http_code == HTTP_CODE_OK) {
        String payload = http.getString();
        // Publish the parsed result under the lock so the render loop never
        // sees a half-cleared array or a torn time value.
        LockWeatherState();
        ParseWeatherReport(payload);
        UnlockWeatherState();
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

// Caller must hold weather_state_mutex: this rewrites the shared weather_report[]
// array and the parsed time/sunrise/sunset globals.
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

#if IS_AIR_SENSOR_ENABLED
// Called from the render loop (ReportAirQuality). Copies the URL into a fixed
// buffer and hands it to the network task. Non-blocking: if the queue is full
// (task wedged), the report is dropped rather than stalling the loop.
void EnqueueAirReport(const String& air_url) {
  AirReport report;
  strncpy(report.url, air_url.c_str(), sizeof(report.url) - 1);
  report.url[sizeof(report.url) - 1] = '\0';
  if (xQueueSend(air_report_queue, &report, 0) != pdTRUE) {
    Serial.println("Air report queue full; dropping this report.");
  }
}

void SendAirReport(String air_url) {
  // wait for WiFi connection
  if ((wifi_multi.run() == WL_CONNECTED)) {

    HTTPClient http;
    http.begin(air_url);
    http.setConnectTimeout(kHttpConnectTimeoutMs);
    http.setTimeout(kHttpTimeoutMs);
    // start connection and send HTTP header
    int http_code = http.GET();

    // http_code will be negative on error
    if (http_code > 0) {
      // HTTP header has been send and Server response header has been handled
      // file found at server
      if (http_code == HTTP_CODE_OK) {
        String payload = http.getString();
        Serial.println(payload);
      }
    } else {
      Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(http_code).c_str());
    }

    http.end();
  }
}
#endif // IS_AIR_SENSOR_ENABLED

// The single owner of all network I/O. Pinned to core 0 in SetupWifi().
void NetworkTask(void* param) {
  while (true) {
    // Refresh weather (and the clock it carries) on a ~30s cadence.
    if (millis() - millis_when_weather_last_fetched > 30000) {
      FetchWeatherReport();
    }

#if IS_AIR_SENSOR_ENABLED
    // Send whatever air reports the render loop has queued since last pass.
    AirReport report;
    while (xQueueReceive(air_report_queue, &report, 0) == pdTRUE) {
      SendAirReport(String(report.url));
    }
#endif // IS_AIR_SENSOR_ENABLED

    // Sleep this task for ~100ms before looping again. FreeRTOS (the real-time
    // OS the ESP32 Arduino core runs on) schedules tasks in "ticks"; on this
    // core one tick is 1ms. pdMS_TO_TICKS(100) converts 100ms to that tick
    // count, so the code is correct regardless of the configured tick rate.
    // vTaskDelay() puts this task in the "blocked" state for that time, using
    // zero CPU while it waits — unlike a busy spin. That handoff lets the idle
    // task run, which is what pets the task watchdog (a safety timer that would
    // reboot the chip if a task hogged its core forever). ~10 checks/second is
    // ample: weather only moves every 30s and air reports every ~5 minutes.
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

#endif // IS_WIFI_ENABLED
