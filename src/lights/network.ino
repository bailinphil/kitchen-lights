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

// Extract the hostname from WEATHER_URL ("http://host/path...") once, so the DNS
// probe below can resolve exactly the host the request uses without hardcoding
// it. Parsed a single time and cached in a static.
const char* WeatherHost() {
  static String host;
  if (host.length() == 0) {
    String url = WEATHER_URL;
    int start = url.indexOf("://");
    start = (start < 0) ? 0 : start + 3;
    int end = url.indexOf('/', start);
    if (end < 0) end = url.length();
    host = url.substring(start, end);
  }
  return host.c_str();
}

// Split an http URL into host and path+query: "http://host/a?b=c" yields
// host="host", path="/a?b=c". Returns false if it isn't an http-style URL.
bool SplitUrl(const String& url, String& host, String& path) {
  int start = url.indexOf("://");
  if (start < 0) return false;
  start += 3;
  int slash = url.indexOf('/', start);
  if (slash < 0) {
    host = url.substring(start);
    path = "/";
  } else {
    host = url.substring(start, slash);
    path = url.substring(slash);
  }
  return host.length() > 0;
}

// Shared last-known-good server address (issue #4). Both WEATHER_URL and AIR_URL
// point at the same host, so one cache serves both. The ESP32 resolver starts
// returning 0.0.0.0 once the DNS record's TTL lapses (dns_ok=1 but dns_ip=
// 0.0.0.0), so we connect straight to this IP instead of trusting live
// resolution. Seeded with the known static droplet IP so the first request works
// before any lookup; FetchWeatherReport refreshes it whenever a lookup returns a
// valid address, and it is never overwritten by a 0.0.0.0. Update this seed if
// the droplet is ever rebuilt.
IPAddress server_ip(161, 35, 100, 35);

// HTTP GET against the cached server IP, bypassing DNS in the request path. We
// can't use HTTPClient here: it derives the Host header from the connect target
// and ignores any Host we set, but the server is name-based virtual-hosted and
// needs Host: <hostname>. So parse the host out of `url`, connect to the cached
// IP, and send the host in the header ourselves. If `payload` is non-null it
// receives the response body (weather); pass nullptr when the body is unneeded
// (air reports). Runs on the network task, so the bounded blocking reads never
// touch the render loop. Returns true on a 200 (and, when a payload was
// requested, a non-empty body).
bool HttpGetViaCachedIp(const String& url, String* payload) {
  String host, path;
  if (!SplitUrl(url, host, path)) return false;

  WiFiClient client;
  if (!client.connect(server_ip, 80, kHttpConnectTimeoutMs)) return false;

  client.print(String("GET ") + path + " HTTP/1.1\r\n"
               + "Host: " + host + "\r\n"
               + "User-Agent: kitchen-lights\r\n"
               + "Connection: close\r\n"
               + "\r\n");

  // Everything below is bounded by this single deadline so a half-open socket
  // can never wedge the task the way the DNS failure used to.
  const unsigned long deadline = millis() + kHttpTimeoutMs;

  // Wait for the first response byte.
  while (client.connected() && client.available() == 0) {
    if ((long)(millis() - deadline) >= 0) { client.stop(); return false; }
    delay(5);
  }

  // Status line, e.g. "HTTP/1.1 200 OK". Anything but 200 is a miss.
  String status_line = client.readStringUntil('\n');
  if (status_line.indexOf(" 200") < 0) { client.stop(); return false; }

  // Skip response headers up to the blank line that ends them.
  while (client.connected() || client.available()) {
    String line = client.readStringUntil('\n');
    if (line.length() <= 1) break;  // just the trailing '\r' -> end of headers
    if ((long)(millis() - deadline) >= 0) { client.stop(); return false; }
  }

  // Body: we asked for "Connection: close", so a static file / short ack arrives
  // as one span the server closes after (no chunking). Read until the socket
  // closes or the deadline trips.
  String body;
  while (client.connected() || client.available()) {
    while (client.available()) {
      body += (char)client.read();
    }
    if (!client.connected()) break;
    if ((long)(millis() - deadline) >= 0) break;
    delay(2);
  }
  client.stop();

  if (payload) {
    if (body.length() == 0) return false;
    *payload = body;
  }
  return true;
}

// Tally one network request outcome for the "Net: XX%" health readout. A
// brown-out that refuses the WiFi connection and a GET that fails mid-request
// both count as a miss here — exactly the symptom we're chasing in issue #4.
void RecordNetworkResult(bool succeeded) {
  network_calls_attempted += 1;
  if (succeeded) {
    network_calls_succeeded += 1;
  }
}

void FetchWeatherReport() {
  // Record the attempt time up front so the retry cadence stays fixed at ~30s
  // even when WiFi is down or the request fails. (Previously this was only
  // updated on success, so during an outage it retried every loop iteration.)
  millis_when_weather_last_fetched = millis();

  bool succeeded = false;
  // Probe DNS every cycle, but only for two side effects: (1) refresh the cached
  // IP when resolution actually returns a valid address, so a rebuilt droplet is
  // picked up automatically while DNS is healthy; (2) keep dns_ok/dns_ip in the
  // diagnostic line so we can still watch the resolver misbehave. The request
  // itself never depends on this succeeding. (issue #4)
  IPAddress dns_ip;
  bool dns_ok = false;

  Serial.println("about to try to use wifi");
  if ((wifi_multi.run() == WL_CONNECTED)) {

    dns_ok = WiFi.hostByName(WeatherHost(), dns_ip);
    if (dns_ok && dns_ip != IPAddress(0, 0, 0, 0)) {
      server_ip = dns_ip;  // last-known-good; never overwritten by a 0.0.0.0
    }

    Serial.print("Requesting ");
    Serial.print(WEATHER_URL);
    Serial.print(" @ ");
    Serial.println(server_ip.toString());

    String payload;
    if (HttpGetViaCachedIp(WEATHER_URL, &payload)) {
      // Publish the parsed result under the lock so the render loop never
      // sees a half-cleared array or a torn time value.
      LockWeatherState();
      ParseWeatherReport(payload);
      UnlockWeatherState();
      succeeded = true;
      Serial.print("Weather report: ");
      Serial.print(millis_when_weather_last_fetched);
      Serial.print(" - ");
      Serial.println(payload);
    } else {
      Serial.println("[HTTP] weather fetch failed");
    }
  }

  RecordNetworkResult(succeeded);

  // One-line health snapshot per attempt. dns_ip going 0.0.0.0 while used_ip
  // holds steady is the fix working: the resolver is failing but we keep hitting
  // the cached address. Other fields still flag heap/stack/link regressions.
  Serial.printf("[net] heap=%u minheap=%u maxblk=%u stackfree=%u status=%d ip=%s rssi=%d dns_ok=%d dns_ip=%s used_ip=%s net=%u/%u\n",
                ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(),
                (unsigned)uxTaskGetStackHighWaterMark(NULL),
                WiFi.status(), WiFi.localIP().toString().c_str(), WiFi.RSSI(),
                dns_ok, dns_ok ? dns_ip.toString().c_str() : "-",
                server_ip.toString().c_str(),
                network_calls_succeeded, network_calls_attempted);
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
  bool succeeded = false;
  // wait for WiFi connection
  if ((wifi_multi.run() == WL_CONNECTED)) {
    // Same cached-IP path as the weather fetch, so air reports survive the DNS
    // TTL failure too (issue #4). The cache is kept fresh by FetchWeatherReport
    // on its 30s cadence; we don't need the response body here.
    succeeded = HttpGetViaCachedIp(air_url, nullptr);
    if (!succeeded) {
      Serial.println("[HTTP] air report failed");
    }
  }

  RecordNetworkResult(succeeded);
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
