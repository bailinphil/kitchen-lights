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
#include <HTTPClient.h>
WiFiMulti wifi_multi;
#include "network_credentials.h"
unsigned long millis_when_weather_last_fetched = 0;
#endif // IS_WIFI_ENABLED

#if IS_AIR_SENSOR_ENABLED
unsigned long millis_when_air_last_reported = 0;
#endif // IS_AIR_SENSOR_ENABLED


/*
 * LED Strip
 */
#if IS_FASTLED_ENABLED
#include <FastLED.h>

// LED chipset configuration
#define CHIPSET     WS2811
#define COLOR_ORDER BRG
constexpr int kBrightness = 20;

// Under-cabinet run (pin 25), wired right-to-left in the real world.
// Logical addressing is left-to-right: D, C, B, A.
constexpr int kUnderCabRight = 25;   // under the rightmost cabinet
constexpr int kOverSink = 25;        // around and over the sink
constexpr int kUnderCabCorner = 25;  // left wall cabinet 1
constexpr int kUnderCabLeft = 25;    // left wall cabinet 2
constexpr int kNumLedsPin25 = kUnderCabRight + kOverSink + kUnderCabCorner + kUnderCabLeft;
CRGB leds_pin25[kNumLedsPin25];

// Ceiling run — two pins acting as one logical strip, left to right.
// Pin 17: wired right-to-left (reversed in software).
// Pin 16: wired left-to-right (natural order).
constexpr int kCeilingLeft = 50;   // pin 17
constexpr int kCeilingRight = 50;  // pin 16
constexpr int kNumLedsPin17 = kCeilingLeft;
constexpr int kNumLedsPin16 = kCeilingRight;
CRGB leds_pin17[kNumLedsPin17];
CRGB leds_pin16[kNumLedsPin16];

CRGB previous_color;
int previous_brightness;
bool is_led_dirty = true;

// Rainbow mode state
constexpr int kRainbowModeIndex = 7;
constexpr unsigned long kRainbowPropagationMs = 3000;
uint8_t rainbow_hue = 0;
bool    rainbow_auto_cycle = true;
unsigned long millis_of_last_rainbow_shift = 0;
int     rainbow_twist_baseline = 0;

// Twinkle mode state
constexpr int kTwinkleModeIndex = 8;
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

// Mode indices
constexpr int kRoutineModeIndex = 1;
constexpr int kNightModeIndex = 5;

// Fire mode state
constexpr int kFireModeIndex = 6;
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
String mode_name[] = {
  "Standby",
  "Routine",
  "Cook Day",
  "Cook Night",
  "Dishes",
  "Night",
  "Fire",
  "Rainbow",
  "Twinkle",
  "Away",
};

#if IS_FASTLED_ENABLED
CRGB mode_color[] = {
  CRGB::Black,       // Standby
  CRGB::White,       // Routine
  CRGB::Seashell,    // Cook Day
  CRGB::LightSalmon, // Cook Night
  CRGB::DarkOrange,  // Dishes
  CRGB::Red,         // Night
  CRGB::OrangeRed,   // Fire (fallback — Fire has its own update path)
  CRGB::Goldenrod,   // Rainbow (unused — Rainbow has its own update path)
  CRGB::ForestGreen, // Twinkle (unused - Twinkle has its own palette)
  CRGB::Black,     // Away
};
#endif

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

uint8_t twist_colors[][3] = {
  {  0,   0,   0},  // Standby
  {100, 100,   0},  // Routine
  {150, 150, 150},  // Cook Day
  {150, 100,  50},  // Cook Night
  {100,  30,   0},  // Dishes
  { 40,   2,   0},  // Night
  {255,  80,   0},  // Fire
  {  0, 255,   0},  // Rainbow
  {  0,   0, 255},  // Twinkle
  {  0,  40,  40},  // Away
};
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
constexpr unsigned long kPresenceTimeoutMs = 20000;        // Night mode: 20 seconds
constexpr unsigned long kRoutinePresenceTimeoutMs = 60000;  // Routine mode: 1 minute
constexpr unsigned long kFadeDurationMs = 3000;
constexpr unsigned long kFadeInDurationMs = 3000;
int ApplyPresenceFade(int brightness, unsigned long timeout = kPresenceTimeoutMs);
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
#endif

#if IS_WIFI_ENABLED
void SetupWifi() {
  wifi_multi.addAP(STA_SSID_HOME, STA_PASS_HOME);
  wifi_multi.addAP(STA_SSID_WORK, STA_PASS_WORK);
  wifi_multi.addAP(STA_SSID_PHONE, STA_PASS_PHONE);
  wifi_multi.addAP(STA_SSID_PROTO, STA_PASS_PROTO);
  millis_when_weather_last_fetched = millis();
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





/*****************************************************************************
 *                                                                           *
 *                                                                           *
 *                                                                           *
 *                                                                           *
 * MAIN LOOP                                                                 *
 *                                                                           *
 *                                                                           *
 *                                                                           *
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
    unsigned long millis_since_presence = millis() - millis_of_last_presence_detection;
    if (millis_since_presence > kPresenceTimeoutMs) {
      millis_of_presence_fade_in_start = millis();
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
      requested_brightness = ApplyPresenceFade(requested_brightness, kRoutinePresenceTimeoutMs);
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
    float amount_fade_complete = 1.0 * (millis_since_presence - timeout) / kFadeDurationMs;
    int brightness_reduction = (int)(255 * amount_fade_complete);
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

