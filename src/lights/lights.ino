// This project uses a half-dozen different libraries and code which derives
// from the examples which come from them. See licenses.h for details.
#include "licenses.h"
#include <Arduino.h>

// The code in this file is provided under the MIT license.

/*
 * Hardware Selection
 */
#define IS_AIR_SENSOR_ENABLED true
#define IS_TWIST_ENABLED      true
#define IS_PRESENCE_ENABLED   true
#define IS_WIFI_ENABLED       true
#define IS_DISPLAY_ENABLED    true
#define IS_FASTLED_ENABLED    true

// The primary I2C bus is needed for display, twist, and air sensor.
// The presence sensor uses a separate I2C bus (Wire1) but still needs Wire.h.
#define IS_I2C_BUS_ENABLED (IS_DISPLAY_ENABLED || IS_TWIST_ENABLED || IS_AIR_SENSOR_ENABLED)

#if IS_I2C_BUS_ENABLED || IS_PRESENCE_ENABLED
#include <Wire.h>
#endif

/*
 * WiFi
 */
#if IS_WIFI_ENABLED
#include <WiFi.h>
#include <WiFiMulti.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
WiFiMulti wifi_multi;
#include "network_credentials.h"
unsigned long millis_when_weather_last_fetched = 0;

// Reliability counters surfaced as "Net: XX%" in the display rotation. Written
// only by the network task (single writer), read by the render loop. Plain
// uint32_t rather than mutex-guarded: aligned 32-bit access is atomic on the
// ESP32, and with one writer neither counter can tear. Added while chasing
// issue #4 (the DNS resolver returning 0.0.0.0 after its record's TTL lapsed);
// kept as an ongoing readout of how often calls are getting through.
uint32_t network_calls_attempted = 0;
uint32_t network_calls_succeeded = 0;

// All outbound network I/O runs on a dedicated FreeRTOS task (see network.ino)
// pinned to core 0, so a slow or failing request can never stall the render
// loop (which runs on core 1). These handles are created in SetupWifi().
SemaphoreHandle_t weather_state_mutex = NULL;  // guards weather_report[] + parsed time ints
QueueHandle_t air_report_queue = NULL;         // render loop -> network task
TaskHandle_t network_task_handle = NULL;

// Air reports are handed to the network task as a fully-formed URL in a fixed
// buffer. FreeRTOS queues copy by value, so we can't pass a heap-backed String.
struct AirReport {
  char url[384];
};
#endif // IS_WIFI_ENABLED

#if IS_AIR_SENSOR_ENABLED
unsigned long millis_when_air_last_reported = 0;
#endif // IS_AIR_SENSOR_ENABLED


/*
 * LED Strip
 */
// Included even when the strips are disabled: kModes (below) uses CRGB.
#include <FastLED.h>

#if IS_FASTLED_ENABLED
// LED chipset configuration
#define CHIPSET     WS2811
#define COLOR_ORDER BRG
constexpr int kBrightness = 20;

// Under-cabinet run (pin 25), wired right-to-left in the real world.
// Logical addressing is left-to-right: D, C, B, A.
constexpr int kUnderCabRight = 19;   // under the rightmost cabinet
constexpr int kSinkRight = 15;       // wall climbing right of the sink
constexpr int kOverSink = 35;        // over the sink
constexpr int kSinkLeft = 15;
constexpr int kUnderCabCorner = 25;  // left wall cabinet 1
constexpr int kStove = 14;
constexpr int kUnderCabLeft = 25;    // left wall cabinet 2
constexpr int kNumLedsPin25 = kUnderCabRight + kSinkRight + kOverSink + 
                              kSinkLeft + kUnderCabCorner + kStove + kUnderCabLeft;
CRGB leds_pin25[kNumLedsPin25];

// Ceiling run — two pins acting as one logical strip, left to right.
// Pin 17: wired right-to-left (reversed in software).
// Pin 16: wired left-to-right (natural order).
constexpr int kCeilingLeft = 147;   // pin 17
constexpr int kCeilingRight = 120;  // pin 16
constexpr int kNumLedsPin17 = kCeilingLeft;
constexpr int kNumLedsPin16 = kCeilingRight;
CRGB leds_pin17[kNumLedsPin17];
CRGB leds_pin16[kNumLedsPin16];

CRGB previous_color;
int previous_brightness;
bool is_led_dirty = true;

// Rainbow mode state
constexpr unsigned long kRainbowPropagationMs = 3000;
uint8_t rainbow_hue = 0;
bool    rainbow_auto_cycle = true;
unsigned long millis_of_last_rainbow_shift = 0;
int     rainbow_twist_baseline = 0;

// Twinkle mode state
constexpr int kTwinkleMaxActive = 30;
constexpr unsigned long kTwinkleSpotLifetime = 1500;    // ms — how long a spot fades in and spreads
constexpr int kTwinkleFadeAmount = 5;                   // per-loop fade; ~2–4s full decay at 5ms loop
constexpr unsigned long kTwinkleDefaultSpawnMs = 73;    // ~15 spawns/sec
constexpr unsigned long kTwinkleMinSpawnMs = 30;        // fastest (~30/sec)
constexpr unsigned long kTwinkleMaxSpawnMs = 500;       // slowest (~2/sec)

struct TwinkleSpot {
  uint8_t strip;          // 0 = pin25, 1 = pin17, 2 = pin16
  uint8_t position;       // physical index into the strip's array
  uint8_t hue;
  unsigned long birth_ms;
  bool active;
};
TwinkleSpot twinkle_spots[kTwinkleMaxActive];

// A small palette of complementary, fully-saturated hues.
// Blues and greens only — avoids jarring hue clashes between neighbors.
// HSV hues: ~80 (green) through ~170 (blue).
const uint8_t twinkle_palette[] = { 80, 96, 110, 128, 145, 160 };
constexpr int kTwinklePaletteSize = sizeof(twinkle_palette);

uint8_t twinkle_mono_hue = 0;
bool    twinkle_monochrome = false;
unsigned long millis_between_twinkle_spawns = kTwinkleDefaultSpawnMs;
unsigned long millis_of_last_twinkle_spawn = 0;
int     twinkle_twist_baseline = 0;

// Fire mode state
constexpr int kFireCooling = 55;           // higher = shorter flames, more cooling per frame
constexpr int kFireSparking = 120;         // chance (out of 255) of a new spark each frame
constexpr int kFireMinSparking = 40;       // calmest embers
constexpr int kFireMaxSparking = 200;      // most intense fire
constexpr unsigned long kFireFrameMs = 15; // ms between simulation steps (~3x slower than loop)

uint8_t heat_pin25[kNumLedsPin25];
uint8_t heat_pin17[kNumLedsPin17];
uint8_t heat_pin16[kNumLedsPin16];

uint8_t fire_sparking = kFireSparking;
bool    fire_cool_palette = false;  // false = warm (red/orange), true = cool (blue/purple)
int     fire_twist_baseline = 0;
unsigned long millis_of_last_fire_frame = 0;
#endif //IS_FASTLED_ENABLED

/*
 * Mode Switching
 */
constexpr int kModeSwitchPin = A5;
int candidate_switch_position = -1;
int committed_switch_position = 0;
unsigned long millis_when_candidate_changed = 0;

// One row per position of the mode switch, in switch order. The index
// constants name the rows that other code refers to; keep them in step with
// the table.
constexpr int kStandbyModeIndex = 0;
constexpr int kRoutineModeIndex = 1;
constexpr int kCookDayModeIndex = 2;
constexpr int kCookNightModeIndex = 3;
constexpr int kDishesModeIndex = 4;
constexpr int kNightModeIndex = 5;
constexpr int kFireModeIndex = 6;
constexpr int kRainbowModeIndex = 7;
constexpr int kTwinkleModeIndex = 8;
constexpr int kAwayModeIndex = 9;

struct ModeInfo {
  const char* name;      // shown on the 16x2 display
  CRGB led_color;        // solid color for the strips
  uint8_t twist_rgb[3];  // color of the Twist knob's LED
};

const ModeInfo kModes[] = {
  {"Standby",    CRGB::Black,       {  0,   0,   0}},
  {"Routine",    CRGB::White,       {100, 100,   0}},  // color overridden by GetRoutineColor()
  {"Cook Day",   CRGB::Seashell,    {150, 150, 150}},
  {"Cook Night", CRGB::LightSalmon, {150, 100,  50}},
  {"Dishes",     CRGB::DarkOrange,  {100,  30,   0}},
  {"Night",      CRGB::Red,         { 40,   2,   0}},
  {"Fire",       CRGB::OrangeRed,   {255,  80,   0}},  // strips have their own update path
  {"Rainbow",    CRGB::Goldenrod,   {  0, 255,   0}},  // strips have their own update path
  {"Twinkle",    CRGB::ForestGreen, {  0,   0, 255}},  // strips have their own palette
  {"Away",       CRGB::Black,       {  0,  40,  40}},
};
constexpr int kNumModes = sizeof(kModes) / sizeof(kModes[0]);
static_assert(kNumModes == 10, "the mode switch has 10 positions (see GetSwitchPosition)");

/*
 * Twist
 */
#if IS_TWIST_ENABLED

// The Twist is used to allow user to control some details of the
// current lighting mode.
#include "SparkFun_Qwiic_Twist_Arduino_Library.h"
TWIST twist;

constexpr int kTwistBrightnessWindowSize = 20;
int twist_brightness_window_center = 0;
int twist_brightness_window_min = - (kTwistBrightnessWindowSize / 2);
int twist_brightness_window_max = (kTwistBrightnessWindowSize / 2);
#endif

constexpr int kWeatherReportMaxLength = 10;

#if IS_DISPLAY_ENABLED
constexpr uint8_t kDisplayAddress = 0x72; // default address of the OpenLCD
// this buffer is used to create helpful debugging messages to send
// over the serial interface.
String previous_message_top = "";
String message_top = "";
String message_bottom = "";
int current_weather_report_display = 1;
unsigned long millis_when_bottom_row_updated = 0;
bool is_display_dirty = true;
unsigned long millis_of_last_display_attempt = 0;
constexpr unsigned long kDisplayRetryInterval = 2000;  // wait 2s before retrying a failed write
#endif // IS_DISPLAY_ENABLED

#if IS_DISPLAY_ENABLED || IS_FASTLED_ENABLED || IS_WIFI_ENABLED
String weather_report[10];
// Parsed from weather_report[0] and the Sunrise/Sunset tokens.
int current_time_hours = -1;
int current_time_minutes = -1;
int sunrise_hours = -1;
int sunrise_minutes = -1;
int sunset_hours = -1;
int sunset_minutes = -1;
#endif // DISPLAY or FASTLED

// Guard the shared weather/time state above: the network task writes it while
// the render loop reads it. Hold time is bounded (a copy or a single parse, no
// blocking calls), so the loop only ever waits microseconds. No-ops when WiFi
// is disabled, since then there is no second thread.
#if IS_WIFI_ENABLED
inline void LockWeatherState()   { if (weather_state_mutex) xSemaphoreTake(weather_state_mutex, portMAX_DELAY); }
inline void UnlockWeatherState() { if (weather_state_mutex) xSemaphoreGive(weather_state_mutex); }
#else
inline void LockWeatherState()   {}
inline void UnlockWeatherState() {}
#endif

/*
 * Presence
 */
#if IS_PRESENCE_ENABLED
#include "SparkFun_STHS34PF80_Arduino_Library.h"
STHS34PF80_I2C presence_sensor;
constexpr int kPresenceSda = 4;
constexpr int kPresenceScl = 13;
constexpr int kPresenceIntPin = 14;  // wired but not yet used; available for interrupt-driven reads

// Values to fill with presence and motion data
int16_t presence_val = 0;
int16_t motion_val = 0;
float temperature_val = 0;
unsigned long millis_of_last_presence_detection = 0;
unsigned long millis_of_presence_fade_in_start = 0;
volatile bool presence_data_ready = false;
constexpr unsigned long kPresenceTimeoutMs = 120000;        // 2 minutes
constexpr unsigned long kFadeDurationMs = 3000;
constexpr unsigned long kFadeInDurationMs = 3000;
constexpr uint16_t kPresenceI2CTimeoutMs = 50;  // matches Wire's own default; set explicitly so it doesn't depend on core defaults
float FadeOutBrightnessRatio(unsigned long fade_out_elapsed);
int ApplyPresenceFade(int brightness, unsigned long timeout = kPresenceTimeoutMs);
void RecoverPresenceI2CBus();
#endif // IS_PRESENCE_ENABLED

/*
 * Air Quality: Particles, Humidity, Temp, voc, NOx
 */
#if IS_AIR_SENSOR_ENABLED
#include <SensirionI2CSen5x.h>
// The used commands use up to 48 bytes. On some Arduino's the default buffer
// space is not large enough
#define MAXBUF_REQUIREMENT 48

#if (defined(I2C_BUFFER_LENGTH) &&                 \
     (I2C_BUFFER_LENGTH >= MAXBUF_REQUIREMENT)) || \
    (defined(BUFFER_LENGTH) && BUFFER_LENGTH >= MAXBUF_REQUIREMENT)
#define USE_PRODUCT_INFO
#endif
SensirionI2CSen5x sen55;

/*
 * Air Quality:
 */
#include "SparkFun_SCD4x_Arduino_Library.h"
SCD4x sen41;
#endif // IS_AIR_SENSOR_ENABLED


/*****************************************************************************
 *                                                                           *
 * SETUP                                                                     *
 *                                                                           *
 ****************************************************************************/

void setup() {
  Serial.begin(115200); // communication speed for debug messages
#if IS_I2C_BUS_ENABLED
  Wire.begin(); // Initialize I2C bus once, before any device setup
#endif
#if IS_FASTLED_ENABLED
  SetupLeds();
#endif
#if IS_DISPLAY_ENABLED
  SetupDisplay();
  delay(50);
#endif
#if IS_TWIST_ENABLED
  SetupTwist();
  delay(50);
#endif
#if IS_PRESENCE_ENABLED
  SetupPresence();
  delay(50);
#endif
#if IS_WIFI_ENABLED
  SetupWifi();
#endif
#if IS_AIR_SENSOR_ENABLED
  delay(50);
  SetupCo2Sensor();
  delay(50);
  SetupParticulateSensor();
#endif // IS_AIR_SENSOR_ENABLED
}

#if IS_FASTLED_ENABLED
void SetupLeds() {
  delay( 3000 ); // power-up safety delay
  // Use pins 25, 17, and 16 because they're adjacent to one another on my board, and
  // Pins 1 and 3 are the default Serial TX and RX pins. This was stomping on debugging.
  FastLED.addLeds<CHIPSET, 25, COLOR_ORDER>(leds_pin25, kNumLedsPin25);  // under-cabinet
  FastLED.addLeds<CHIPSET, 17, COLOR_ORDER>(leds_pin17, kNumLedsPin17);  // ceiling left
  FastLED.addLeds<CHIPSET, 16, COLOR_ORDER>(leds_pin16, kNumLedsPin16);  // ceiling right
  FastLED.setBrightness( kBrightness );
  previous_brightness = kBrightness;
}
#endif // IS_FASTLED_ENABLED

#if IS_DISPLAY_ENABLED
void SetupDisplay() {
  // Wire.begin() is called once in setup() before any device init.
  //Send the reset command to the display - this forces the cursor to return to the beginning of the display
  Wire.beginTransmission(kDisplayAddress);
  Wire.write('|'); // Put LCD into setting mode
  Wire.write(160); // turn down the green of the backlight
  Wire.write(188); // turn off the blue of the backlight
  Wire.write('-'); // Send clear display command
  Wire.endTransmission();

  for (int i = 0; i < kWeatherReportMaxLength; ++i) {
    weather_report[i] = "";
  }
}
#endif // IS_DISPLAY_ENABLED

#if IS_TWIST_ENABLED
void SetupTwist() {
  if (twist.begin() == false) {
    Serial.println("Twist does not appear to be connected. Please check wiring. Freezing...");
    while (1);
  }

  twist_brightness_window_center = twist.getCount();
  twist_brightness_window_min = twist_brightness_window_center - (kTwistBrightnessWindowSize / 2);
  twist_brightness_window_max = twist_brightness_window_center + (kTwistBrightnessWindowSize / 2);
  twist.setColor(100,10,0);
}
#endif

#if IS_PRESENCE_ENABLED
// https://github.com/sparkfun/SparkFun_STHS34PF80_Arduino_Library
void IRAM_ATTR OnPresenceInterrupt() {
  presence_data_ready = true;
}

void SetupPresence() {
    Wire1.begin(kPresenceSda, kPresenceScl);
    Wire1.setTimeOut(kPresenceI2CTimeoutMs);
    // Establish communication with device on its own I2C bus
    if (presence_sensor.begin(STHS34PF80_I2C_ADDRESS, Wire1) == false) {
      Serial.println("Error setting up presence sensor - please check wiring.");
      while(1);
    }

    // Route presence+motion flags to the INT pin, latched until
    // the status register is read.
    presence_sensor.setTmosRouteInterrupt(STHS34PF80_TMOS_INT_OR);
    presence_sensor.setTmosInterruptOR(STHS34PF80_TMOS_INT_MOTION_PRESENCE);
    presence_sensor.setInterruptPulsed(1);  // latched: INT stays low until status is read

    pinMode(kPresenceIntPin, INPUT);
    attachInterrupt(digitalPinToInterrupt(kPresenceIntPin), OnPresenceInterrupt, FALLING);
}

// Called whenever a presence I2C call comes back with an error. Wire1's own
// timeout (see kPresenceI2CTimeoutMs) stops a single call from blocking
// forever, but it doesn't put the bus back into a usable state: a slave that
// faulted mid-byte can be left holding SDA low, which fails every subsequent
// transaction until something manually clocks it free. This does the
// standard I2C bus-recovery dance and re-establishes the Wire1 peripheral.
void RecoverPresenceI2CBus() {
  Serial.print(millis());
  Serial.println(": Recovering presence I2C bus (Wire1)");

  Wire1.end();

  pinMode(kPresenceScl, OUTPUT);
  pinMode(kPresenceSda, INPUT_PULLUP);

  // A slave stuck mid-byte holds SDA low. Toggling SCL up to 9 times (one per
  // bit of the largest possible transfer) lets it finish clocking out
  // whatever it was sending and release the line.
  for (int i = 0; i < 9 && digitalRead(kPresenceSda) == LOW; i++) {
    digitalWrite(kPresenceScl, LOW);
    delayMicroseconds(5);
    digitalWrite(kPresenceScl, HIGH);
    delayMicroseconds(5);
  }

  // Leave the bus in a clean idle state with a manual STOP condition (SDA
  // rising while SCL is high) before handing it back to the Wire driver.
  pinMode(kPresenceSda, OUTPUT);
  digitalWrite(kPresenceSda, LOW);
  delayMicroseconds(5);
  digitalWrite(kPresenceScl, HIGH);
  delayMicroseconds(5);
  digitalWrite(kPresenceSda, HIGH);
  delayMicroseconds(5);

  Wire1.begin(kPresenceSda, kPresenceScl);
  Wire1.setTimeOut(kPresenceI2CTimeoutMs);
}
#endif

#if IS_WIFI_ENABLED
void SetupWifi() {
  wifi_multi.addAP(STA_SSID_HOME, STA_PASS_HOME);
  wifi_multi.addAP(STA_SSID_WORK, STA_PASS_WORK);
  wifi_multi.addAP(STA_SSID_PHONE, STA_PASS_PHONE);
  wifi_multi.addAP(STA_SSID_PROTO, STA_PASS_PROTO);
  millis_when_weather_last_fetched = millis();

  weather_state_mutex = xSemaphoreCreateMutex();
  air_report_queue = xQueueCreate(2, sizeof(AirReport));

  // Spawn the background network task. FreeRTOS (the OS underneath the Arduino
  // core) runs it concurrently with loop(); we pin it to core 0 (PRO_CPU) while
  // the Arduino loop() runs on core 1 (APP_CPU), so blocking HTTP here never
  // stalls rendering or input. The arguments, in order, are:
  //   NetworkTask          - the function to run as the task (never returns)
  //   "network"            - a name, used only in debug output / crash traces
  //   8192                 - stack size in bytes reserved for this task
  //   NULL                 - optional argument passed to the task (we need none)
  //   1                    - priority (same as the Arduino loop task)
  //   &network_task_handle - out-param: receives a handle to the created task
  //   0                    - which core to pin it to (0 = PRO_CPU)
  xTaskCreatePinnedToCore(NetworkTask, "network", 8192, NULL, 1, &network_task_handle, 0);
}
#endif // IS_WIFI_ENABLED

#if IS_AIR_SENSOR_ENABLED
void SetupCo2Sensor() {
  //sen41.enableDebugging(); // Uncomment this line to get helpful debug messages on Serial

  //.begin will start periodic measurements for us (see the later examples for details on how to override this)
  if (sen41.begin() == false) {
    Serial.println(F("Sensor not detected. Please check wiring. Freezing..."));
    while (1);
  }
}

void SetupParticulateSensor() {
    sen55.begin(Wire);

    uint16_t error;
    char error_message[256];
    error = sen55.deviceReset();
    if (error) {
        Serial.print("Error trying to execute deviceReset(): ");
        errorToString(error, error_message, 256);
        Serial.println(error_message);
    }

    // The SEN55 needs time to boot after a reset before it will
    // respond to I2C commands. Without this delay, subsequent reads
    // fail with CRC or write errors.
    delay(1000);

// Print SEN55 module information if i2c buffers are large enough
#ifdef USE_PRODUCT_INFO
    PrintSerialNumber();
    PrintModuleVersions();
#endif // USE_PRODUCT_INFO

    float temp_offset = 0.0;
    error = sen55.setTemperatureOffsetSimple(temp_offset);
    if (error) {
        Serial.print("Error trying to execute setTemperatureOffsetSimple(): ");
        errorToString(error, error_message, 256);
        Serial.println(error_message);
    } else {
        Serial.print("Temperature Offset set to ");
        Serial.print(temp_offset);
        Serial.println(" deg. Celsius (SEN54/SEN55 only");
    }

    // Start Measurement
    error = sen55.startMeasurement();
    if (error) {
        Serial.print("Error trying to execute startMeasurement(): ");
        errorToString(error, error_message, 256);
        Serial.println(error_message);
    }
}

void PrintSerialNumber() {
    uint16_t error;
    char error_message[256];
    unsigned char serialNumber[32];
    uint8_t serial_number_size = 32;

    error = sen55.getSerialNumber(serialNumber, serial_number_size);
    if (error) {
        Serial.print("Error trying to execute getSerialNumber(): ");
        errorToString(error, error_message, 256);
        Serial.println(error_message);
    } else {
        Serial.print("SerialNumber:");
        Serial.println((char*)serialNumber);
    }
}

void PrintModuleVersions() {
    uint16_t error;
    char error_message[256];

    unsigned char productName[32];
    uint8_t product_name_size = 32;

    error = sen55.getProductName(productName, product_name_size);

    if (error) {
        Serial.print("Error trying to execute getProductName(): ");
        errorToString(error, error_message, 256);
        Serial.println(error_message);
    } else {
        Serial.print("ProductName:");
        Serial.println((char*)productName);
    }

    uint8_t firmware_major;
    uint8_t firmware_minor;
    bool firmware_debug;
    uint8_t hardware_major;
    uint8_t hardware_minor;
    uint8_t protocol_major;
    uint8_t protocol_minor;

    error = sen55.getVersion(firmware_major, firmware_minor, firmware_debug,
                             hardware_major, hardware_minor, protocol_major,
                             protocol_minor);
    if (error) {
        Serial.print("Error trying to execute getVersion(): ");
        errorToString(error, error_message, 256);
        Serial.println(error_message);
    } else {
        Serial.print("Firmware: ");
        Serial.print(firmware_major);
        Serial.print(".");
        Serial.print(firmware_minor);
        Serial.print(", ");

        Serial.print("Hardware: ");
        Serial.print(hardware_major);
        Serial.print(".");
        Serial.println(hardware_minor);
    }
}

#endif // IS_AIR_SENSOR_ENABLED
