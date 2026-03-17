// This project uses a half-dozen different libraries and code which derives
// from the examples which come from them. See licenses.h for details.
#include "licenses.h"
#include <Arduino.h>

/*
 * Hardware Selection
 */
#define IS_AIR_SENSOR_ENABLED true
#define IS_TWIST_ENABLED      true
#define IS_PRESENCE_ENABLED   true
#define IS_WIFI_ENABLED       true
#define IS_DISPLAY_ENABLED    true
#define IS_FASTLED_ENABLED    true

/*
 * WiFi
 */
#if IS_WIFI_ENABLED
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#define USE_SERIAL Serial
WiFiMulti wifiMulti;
#include "network_credentials.h"
unsigned long millisWhenWeatherLastFetched = 0;
#endif // IS_WIFI_ENABLED

#if IS_AIR_SENSOR_ENABLED
unsigned long millisWhenAirLastReported = 0;
#endif // IS_AIR_SENSOR_ENABLED


/*
 * LED Strip
 */
#if IS_FASTLED_ENABLED
#include <FastLED.h>

// LED chipset configuration
#define CHIPSET     WS2811
#define COLOR_ORDER BRG
#define BRIGHTNESS  20

// Under-cabinet run (pin 25), wired right-to-left in the real world.
// Logical addressing is left-to-right: D, C, B, A.
#define UNDER_CAB_RIGHT  25  // under the rightmost cabinet
#define OVER_SINK  25  // around and over the sink
#define UNDER_CAB_CORNER  25  // left wall cabinet 1
#define UNDER_CAB_LEFT  25  // left wall cabinet 2
#define NUM_LEDS_PIN25 (UNDER_CAB_RIGHT + OVER_SINK + UNDER_CAB_CORNER + UNDER_CAB_LEFT)
CRGB ledsPin25[NUM_LEDS_PIN25];

// Ceiling run — two pins acting as one logical strip, left to right.
// Pin 17: wired right-to-left (reversed in software).
// Pin 16: wired left-to-right (natural order).
#define CEILING_LEFT  50  // pin 17
#define CEILING_RIGHT  50  // pin 16
#define NUM_LEDS_PIN17 CEILING_LEFT
#define NUM_LEDS_PIN16 CEILING_RIGHT
CRGB ledsPin17[NUM_LEDS_PIN17];
CRGB ledsPin16[NUM_LEDS_PIN16];

CRGB previousColor;
int previousBrightness;
bool isLedDirty = true;

// Rainbow mode state
#define RAINBOW_MODE_INDEX      7
#define RAINBOW_PROPAGATION_MS  3000
uint8_t rainbowHue = 0;
bool    rainbowAutoCycle = true;
unsigned long millisOfLastRainbowShift = 0;
int     rainbowTwistBaseline = 0;

// Twinkle mode state
#define TWINKLE_MODE_INDEX      8
#define TWINKLE_MAX_ACTIVE      30
#define TWINKLE_SPOT_LIFETIME   1500   // ms — how long a spot fades in and spreads
#define TWINKLE_FADE_AMOUNT     5    // per-loop fade; ~2–4s full decay at 5ms loop
#define TWINKLE_DEFAULT_SPAWN_MS 73  // ~15 spawns/sec
#define TWINKLE_MIN_SPAWN_MS    30    // fastest (~30/sec)
#define TWINKLE_MAX_SPAWN_MS    500   // slowest (~2/sec)

struct TwinkleSpot {
  uint8_t strip;          // 0 = pin25, 1 = pin17, 2 = pin16
  uint8_t position;       // physical index into the strip's array
  uint8_t hue;
  unsigned long birthMs;
  bool active;
};
TwinkleSpot twinkleSpots[TWINKLE_MAX_ACTIVE];

// A small palette of complementary, fully-saturated hues.
// Blues and greens only — avoids jarring hue clashes between neighbors.
// HSV hues: ~80 (green) through ~170 (blue).
const uint8_t twinklePalette[] = { 80, 96, 110, 128, 145, 160 };
#define TWINKLE_PALETTE_SIZE 6

uint8_t twinkleMonoHue = 0;
bool    twinkleMonochrome = false;
unsigned long millisBetweenTwinkleSpawns = TWINKLE_DEFAULT_SPAWN_MS;
unsigned long millisOfLastTwinkleSpawn = 0;
int     twinkleTwistBaseline = 0;
#endif //IS_FASTLED_ENABLED

/*
 * Mode Switching
 */
#define MODE_SWITCH_PIN A5
int candidateSwitchPosition = -1;
int committedSwitchPosition = 0;
unsigned long millisWhenCandidateChanged = 0;
String modeName[] = {
  "Standby",
  "Routine",
  "Cook Day",
  "Cook Night",
  "Dishes",
  "Night",
  "Sparkles",
  "Rainbow",
  "Twinkle",
  "Away",
};

#if IS_FASTLED_ENABLED
CRGB modeColor[] = {
  CRGB::Black,       // Standby
  CRGB::White,       // Routine
  CRGB::Green,       // Cook Day
  CRGB::Blue,        // Cook Night
  CRGB::Orange,      // Dishes
  CRGB::Red,         // Night
  CRGB::BlueViolet,  // Sparkles
  CRGB::Goldenrod,   // Rainbow (unused — Rainbow has its own update path)
  CRGB::ForestGreen, // Twinkle
  CRGB::DimGray,     // Away
  CRGB::Black,
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

#define twistBrightnessWindowSize 20
int twistBrightnessWindowCenter = 0;
int twistBrightnessWindowMin = - (twistBrightnessWindowSize / 2);
int twistBrightnessWindowMax = (twistBrightnessWindowSize / 2);

uint8_t twist_colors[][3] = {
  {  0,   0,   0},  // Standby
  {100, 100,   0},  // Routine
  {150, 150, 150},  // Cook Day
  {150, 100,  50},  // Cook Night
  {100,  30,   0},  // Dishes
  { 40,   2,   0},  // Night
  {255,   0,   0},  // Sparkles
  {  0, 255,   0},  // Rainbow
  {  0,   0, 255},  // Twinkle
  {  0,  40,  40},  // Away
};
#endif


#if IS_DISPLAY_ENABLED
// Wire.h is used to write to the 16x2 display which gives the user
// information about what's going on with the lights, as well as
// information about air quality in the room.
#include <Wire.h>
#define DISPLAY_ADDRESS1 0x72 //This is the default address of the OpenLCD
// this buffer is used to create helpful debugging messages to send
// over the serial interface.
String previousMessageTop = "";
String messageTop = "";
String messageBottom = "";
String weatherReport[10];
int currentWeatherReportDisplay = 1;
unsigned long millisWhenBottomRowUpdated = 0;
#define WEATHER_REPORT_MAX_LENGTH 10
bool isDisplayDirty = true;
#endif // IS_DISPLAY_ENABLED

/*
 * Presence
 */
#if IS_PRESENCE_ENABLED
#include "SparkFun_STHS34PF80_Arduino_Library.h"
STHS34PF80_I2C mySensor;

// Values to fill with presence and motion data
int16_t presenceVal = 0;
int16_t motionVal = 0;
float temperatureVal = 0;
unsigned long millisOfLastPresenceDetection = 0;
#define millisTimeoutAtNight 20000
#define millisFadeDuration 3000
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
#if IS_FASTLED_ENABLED
  setupLEDs();
#endif
#if IS_DISPLAY_ENABLED
  setupDisplay();
#endif
#if IS_TWIST_ENABLED
  setupTwist();
#endif
#if IS_PRESENCE_ENABLED
  setupPresence();
#endif
#if IS_WIFI_ENABLED
  setupWiFi();
#endif
#if IS_AIR_SENSOR_ENABLED
  setupCO2Sensor();
  setupParticulateSensor();
#endif // IS_AIR_SENSOR_ENABLED
}

#if IS_FASTLED_ENABLED
void setupLEDs() {
  delay( 3000 ); // power-up safety delay
  // Use pins 25, 17, and 16 because they're adjacent to one another on my board, and
  // Pins 1 and 3 are the default Serial TX and RX pins. This was stomping on debugging.
  FastLED.addLeds<CHIPSET, 25, COLOR_ORDER>(ledsPin25, NUM_LEDS_PIN25);  // under-cabinet
  FastLED.addLeds<CHIPSET, 17, COLOR_ORDER>(ledsPin17, NUM_LEDS_PIN17);  // ceiling left
  FastLED.addLeds<CHIPSET, 16, COLOR_ORDER>(ledsPin16, NUM_LEDS_PIN16);  // ceiling right
  FastLED.setBrightness( BRIGHTNESS );
  previousBrightness = BRIGHTNESS;
}
#endif // IS_FASTLED_ENABLED

#if IS_DISPLAY_ENABLED
void setupDisplay() {
  Wire.begin(); //Join the bus as master
  //Send the reset command to the display - this forces the cursor to return to the beginning of the display
  Wire.beginTransmission(DISPLAY_ADDRESS1);
  Wire.write('|'); // Put LCD into setting mode
  Wire.write(160); // turn down the green of the backlight
  Wire.write(188); // turn off the blue of the backlight
  Wire.write('-'); // Send clear display command
  Wire.endTransmission();

  for (int i = 0; i < WEATHER_REPORT_MAX_LENGTH; ++i) {
    weatherReport[i] = "";
  }
}
#endif // IS_DISPLAY_ENABLED

#if IS_TWIST_ENABLED
void setupTwist() {
  if (twist.begin() == false)
  {
    Serial.println("Twist does not appear to be connected. Please check wiring. Freezing...");
    while (1);
  }

  twistBrightnessWindowCenter = twist.getCount();
  twistBrightnessWindowMin = twistBrightnessWindowCenter - (twistBrightnessWindowSize / 2);
  twistBrightnessWindowMax = twistBrightnessWindowCenter + (twistBrightnessWindowSize / 2);
  twist.setColor(100,10,0);
}
#endif

#if IS_PRESENCE_ENABLED
// https://github.com/sparkfun/SparkFun_STHS34PF80_Arduino_Library
void setupPresence() {
    // Establish communication with device
    if (mySensor.begin() == false)
    {
      Serial.println("Error setting up device - please check wiring.");
      while(1);
    }
}
#endif

#if IS_WIFI_ENABLED
void setupWiFi() {
  wifiMulti.addAP(STA_SSID_HOME, STA_PASS_HOME);
  wifiMulti.addAP(STA_SSID_WORK, STA_PASS_WORK);
  wifiMulti.addAP(STA_SSID_PHONE, STA_PASS_PHONE);
  wifiMulti.addAP(STA_SSID_PROTO, STA_PASS_PROTO);
  millisWhenWeatherLastFetched = millis();
}
#endif // IS_WIFI_ENABLED

#if IS_AIR_SENSOR_ENABLED
void setupCO2Sensor() {
  //sen41.enableDebugging(); // Uncomment this line to get helpful debug messages on Serial

  //.begin will start periodic measurements for us (see the later examples for details on how to override this)
  if (sen41.begin() == false)
  {
    Serial.println(F("Sensor not detected. Please check wiring. Freezing..."));
    while (1);
  }
}

void setupParticulateSensor() {
    sen55.begin(Wire);

    uint16_t error;
    char errorMessage[256];
    error = sen55.deviceReset();
    if (error) {
        Serial.print("Error trying to execute deviceReset(): ");
        errorToString(error, errorMessage, 256);
        Serial.println(errorMessage);
    }

// Print SEN55 module information if i2c buffers are large enough
#ifdef USE_PRODUCT_INFO
    printSerialNumber();
    printModuleVersions();
#endif // USE_PRODUCT_INFO

    float tempOffset = 0.0; 
    error = sen55.setTemperatureOffsetSimple(tempOffset);
    if (error) {
        Serial.print("Error trying to execute setTemperatureOffsetSimple(): ");
        errorToString(error, errorMessage, 256);
        Serial.println(errorMessage);
    } else {
        Serial.print("Temperature Offset set to ");
        Serial.print(tempOffset);
        Serial.println(" deg. Celsius (SEN54/SEN55 only");
    }

    // Start Measurement
    error = sen55.startMeasurement();
    if (error) {
        Serial.print("Error trying to execute startMeasurement(): ");
        errorToString(error, errorMessage, 256);
        Serial.println(errorMessage);
    }
}

void printSerialNumber() {
    uint16_t error;
    char errorMessage[256];
    unsigned char serialNumber[32];
    uint8_t serialNumberSize = 32;

    error = sen55.getSerialNumber(serialNumber, serialNumberSize);
    if (error) {
        Serial.print("Error trying to execute getSerialNumber(): ");
        errorToString(error, errorMessage, 256);
        Serial.println(errorMessage);
    } else {
        Serial.print("SerialNumber:");
        Serial.println((char*)serialNumber);
    }
}

void printModuleVersions() {
    uint16_t error;
    char errorMessage[256];

    unsigned char productName[32];
    uint8_t productNameSize = 32;

    error = sen55.getProductName(productName, productNameSize);

    if (error) {
        Serial.print("Error trying to execute getProductName(): ");
        errorToString(error, errorMessage, 256);
        Serial.println(errorMessage);
    } else {
        Serial.print("ProductName:");
        Serial.println((char*)productName);
    }

    uint8_t firmwareMajor;
    uint8_t firmwareMinor;
    bool firmwareDebug;
    uint8_t hardwareMajor;
    uint8_t hardwareMinor;
    uint8_t protocolMajor;
    uint8_t protocolMinor;

    error = sen55.getVersion(firmwareMajor, firmwareMinor, firmwareDebug,
                             hardwareMajor, hardwareMinor, protocolMajor,
                             protocolMinor);
    if (error) {
        Serial.print("Error trying to execute getVersion(): ");
        errorToString(error, errorMessage, 256);
        Serial.println(errorMessage);
    } else {
        Serial.print("Firmware: ");
        Serial.print(firmwareMajor);
        Serial.print(".");
        Serial.print(firmwareMinor);
        Serial.print(", ");

        Serial.print("Hardware: ");
        Serial.print(hardwareMajor);
        Serial.print(".");
        Serial.println(hardwareMinor);
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

void loop()
{
#if IS_AIR_SENSOR_ENABLED
  unsigned long millisSinceAirReport = millis() - millisWhenAirLastReported;
  if (millisSinceAirReport > 120500) {
    reportAirQuality();
    millisWhenAirLastReported = millis();
  }
#endif // IS_AIR_SENSOR_ENABLED

#if IS_WIFI_ENABLED
  unsigned long millisSinceWeatherFetch = millis() - millisWhenWeatherLastFetched;
  if (millisSinceWeatherFetch > 30000) {
    fetchWeatherReport();
  }
#endif // IS_WIFI_ENABLED

#if IS_TWIST_ENABLED
  if (twist.isClicked()) {
#if IS_FASTLED_ENABLED
    if (committedSwitchPosition == RAINBOW_MODE_INDEX) {
      // In Rainbow mode, the button toggles auto-cycle on/off.
      rainbowAutoCycle = !rainbowAutoCycle;
    } else if (committedSwitchPosition == TWINKLE_MODE_INDEX) {
      // In Twinkle mode, the button toggles monochrome on/off.
      twinkleMonochrome = !twinkleMonochrome;
      if (twinkleMonochrome) {
        // Latch the most recently used palette hue.
        twinkleMonoHue = twinklePalette[random(TWINKLE_PALETTE_SIZE)];
      }
    } else
#endif // IS_FASTLED_ENABLED
    {
      twistBrightnessWindowCenter = twist.getCount();
      twistBrightnessWindowMin = twist.getCount() - twistBrightnessWindowSize;
      twistBrightnessWindowMax = twist.getCount() + twistBrightnessWindowSize;
    }
  }
#endif

  // figure out what position the switch is in, while ignoring noise
  int nextSwitchPosition = getDebouncedSwitchPosition();

#if IS_PRESENCE_ENABLED
  int detectedPresence = checkPresence();
  if (detectedPresence != 0) {
    millisOfLastPresenceDetection = millis();
  }
#endif

#if IS_TWIST_ENABLED
  int currentTwistPosition = twist.getCount();

#if IS_FASTLED_ENABLED
  if (nextSwitchPosition == RAINBOW_MODE_INDEX) {
    // In Rainbow mode the twist controls hue, not brightness.
    int twistDelta = currentTwistPosition - rainbowTwistBaseline;
    rainbowHue += (uint8_t)twistDelta;  // wraps naturally
    rainbowTwistBaseline = currentTwistPosition;
  } else if (nextSwitchPosition == TWINKLE_MODE_INDEX) {
    // In Twinkle mode the twist controls spawn rate.
    int twistDelta = currentTwistPosition - twinkleTwistBaseline;
    // Each tick adjusts spawn interval by ~10ms.
    int newInterval = (int)millisBetweenTwinkleSpawns - (twistDelta * 10);
    millisBetweenTwinkleSpawns = constrain(newInterval, TWINKLE_MIN_SPAWN_MS, TWINKLE_MAX_SPAWN_MS);
    twinkleTwistBaseline = currentTwistPosition;
  } else
#endif // IS_FASTLED_ENABLED
  {
    // Normal modes: twist controls brightness.
    clampTwistWindow(currentTwistPosition);
    float twistedPositionInWindow = (currentTwistPosition - twistBrightnessWindowMin) / (1.0 * twistBrightnessWindowSize);
    int requestedBrightness = (int)(255 * twistedPositionInWindow);

#if IS_PRESENCE_ENABLED
    // In Night mode, turn off the lights when no presence has been detected for a while.
    if (nextSwitchPosition == 5) {
      requestedBrightness = applyNightFade(requestedBrightness);
    }
#endif // IS_PRESENCE_ENABLED

#if IS_FASTLED_ENABLED
    if(requestedBrightness != previousBrightness){
      FastLED.setBrightness(requestedBrightness);
      previousBrightness = requestedBrightness;
      isLedDirty = true;
    }
#endif // IS_FASTLED_ENABLED
  }
  uint8_t* tc = twist_colors[nextSwitchPosition];
  twist.setColor(tc[0],tc[1],tc[2]);
#endif // IS_TWIST_ENABLED

#if IS_DISPLAY_ENABLED
  messageTop = prepareTopMessage(nextSwitchPosition);
  messageBottom = prepareBottomMessage();
  if (isDisplayDirty) {
    updateDisplay(messageTop, messageBottom);
  }
#endif // IS_DISPLAY_ENABLED

#if IS_FASTLED_ENABLED
  if (nextSwitchPosition == RAINBOW_MODE_INDEX) {
    updateRainbow();
  } else if (nextSwitchPosition == TWINKLE_MODE_INDEX) {
    updateTwinkle();
  } else if (isLedDirty) {
    setAllLEDs(modeColor[nextSwitchPosition]);
    FastLED.show();
    isLedDirty = false;
  }
#endif // IS_FASTLED_ENABLED

  delay(5);
}


/*****************************************************************************
 *                                                                           *
 * DISPLAY                                                                   *
 *                                                                           *
 ****************************************************************************/
#if IS_DISPLAY_ENABLED
String prepareTopMessage(uint8_t switchPos) {
  String result = weatherReport[0] + " " + modeName[switchPos];
  if (!result.equals(previousMessageTop)) {
    isDisplayDirty = true;
    previousMessageTop = result;
  }
  return result;
}

String prepareBottomMessage() {
  if (currentWeatherReportDisplay >= WEATHER_REPORT_MAX_LENGTH ||
      weatherReport[currentWeatherReportDisplay].length() == 0) {
    currentWeatherReportDisplay = 1;
  }
  String result = weatherReport[currentWeatherReportDisplay];
  if (millis() - millisWhenBottomRowUpdated > 3000) {
    millisWhenBottomRowUpdated = millis();
    currentWeatherReportDisplay += 1;
    isDisplayDirty = true;
  }
  return result;
}

void updateDisplay(String messageTop, String messageBottom) {
  Serial.println(messageTop);
  Serial.println(messageBottom);
  Wire.beginTransmission(DISPLAY_ADDRESS1); // transmit to device #1

  Wire.write('|'); //Put LCD into setting mode
  Wire.write('-'); //Send clear display command

  Wire.print(messageTop);
  unsigned int charsPrinted = messageTop.length();
  while (charsPrinted < 16) {
    Wire.print(" ");
    charsPrinted++;
  }
  Wire.print(messageBottom);

  uint8_t i2cError = Wire.endTransmission(); //Stop I2C transmission
  if (i2cError != 0) {
    Serial.print("I2C error on display: ");
    Serial.println(i2cError);
  } else {
    isDisplayDirty = false;
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
void setUnderCabinetLEDs(CRGB color) {
  for (int i = 0; i < NUM_LEDS_PIN25; i++) {
    ledsPin25[NUM_LEDS_PIN25 - 1 - i] = color;
  }
}

// Fill the ceiling strips (pins 17 + 16) as one continuous run.
// Pin 17 is wired right-to-left (reversed here), then pin 16 continues
// left-to-right (natural order), so an animation starting at logical
// position 0 flows seamlessly from pin 17 into pin 16.
void setCeilingLEDs(CRGB color) {
  for (int i = 0; i < NUM_LEDS_PIN17; i++) {
    ledsPin17[NUM_LEDS_PIN17 - 1 - i] = color;
  }
  for (int i = 0; i < NUM_LEDS_PIN16; i++) {
    ledsPin16[i] = color;
  }
}

// Fill every LED on every strip with the same color.
void setAllLEDs(CRGB color) {
  setUnderCabinetLEDs(color);
  setCeilingLEDs(color);
}

// Shift the under-cabinet strip one position to the right (logically,
// left-to-right) and inject a new color at the leftmost end.
// Physical wiring is right-to-left, so leftmost = highest index.
void shiftUnderCabinetRight(CRGB newColor) {
  for (int i = 0; i < NUM_LEDS_PIN25 - 1; i++) {
    ledsPin25[i] = ledsPin25[i + 1];
  }
  ledsPin25[NUM_LEDS_PIN25 - 1] = newColor;
}

// Shift the ceiling strips one position to the right (logically) and
// inject a new color at the leftmost end.  Pin 17 (reversed wiring)
// feeds into pin 16 (natural wiring) so the animation crosses over.
void shiftCeilingRight(CRGB newColor) {
  // Shift pin 16 right (natural order, increasing index)
  for (int i = NUM_LEDS_PIN16 - 1; i > 0; i--) {
    ledsPin16[i] = ledsPin16[i - 1];
  }
  // Crossover: pin 17 rightmost logical (physical index 0) → pin 16 leftmost
  ledsPin16[0] = ledsPin17[0];
  // Shift pin 17 right in logical order (decreasing physical index)
  for (int i = 0; i < NUM_LEDS_PIN17 - 1; i++) {
    ledsPin17[i] = ledsPin17[i + 1];
  }
  // Inject at pin 17 leftmost logical (physical index N-1)
  ledsPin17[NUM_LEDS_PIN17 - 1] = newColor;
}

// Rainbow animation: shift all strips and inject the current hue.
// Called every loop() iteration when in Rainbow mode; the shift only
// happens when enough time has elapsed for one propagation step.
void updateRainbow() {
  unsigned long now = millis();
  int longestRun = max((int)NUM_LEDS_PIN25, (int)(NUM_LEDS_PIN17 + NUM_LEDS_PIN16));
  unsigned long shiftInterval = RAINBOW_PROPAGATION_MS / longestRun;

  if (now - millisOfLastRainbowShift >= shiftInterval) {
    millisOfLastRainbowShift = now;
    CRGB color = CHSV(rainbowHue, 255, 255);
    shiftUnderCabinetRight(color);
    shiftCeilingRight(color);
    if (rainbowAutoCycle) {
      rainbowHue++;  // wraps naturally at 256 → 0
    }
    FastLED.show();
  }
}

// --- Twinkle helpers ---

// Write a color to a physical LED position, clamping to strip bounds.
void setStripPixel(uint8_t strip, int pos, CRGB color) {
  switch (strip) {
    case 0:
      if (pos >= 0 && pos < NUM_LEDS_PIN25) ledsPin25[pos] = color;
      break;
    case 1:
      if (pos >= 0 && pos < NUM_LEDS_PIN17) ledsPin17[pos] = color;
      break;
    case 2:
      if (pos >= 0 && pos < NUM_LEDS_PIN16) ledsPin16[pos] = color;
      break;
  }
}

// Return the length of a strip by index.
int stripLength(uint8_t strip) {
  switch (strip) {
    case 0: return NUM_LEDS_PIN25;
    case 1: return NUM_LEDS_PIN17;
    case 2: return NUM_LEDS_PIN16;
    default: return 0;
  }
}

// Check whether a candidate position on a strip is too close to an
// active spot that still has significant lifetime remaining.
bool isTooCloseToActiveSpot(uint8_t strip, uint8_t pos) {
  unsigned long now = millis();
  for (int i = 0; i < TWINKLE_MAX_ACTIVE; i++) {
    if (!twinkleSpots[i].active) continue;
    if (twinkleSpots[i].strip != strip) continue;
    // "Significant time left" = less than halfway through its lifetime.
    unsigned long age = now - twinkleSpots[i].birthMs;
    if (age > TWINKLE_SPOT_LIFETIME / 2) continue;
    // Too close if within ±2 of an active bright spot.
    int dist = abs((int)pos - (int)twinkleSpots[i].position);
    if (dist <= 2) return true;
  }
  return false;
}

// Spawn a new twinkle spot in the first available slot.
// Tries a few random positions to avoid landing on or next to a
// still-bright spot; gives up after a handful of attempts.
#define TWINKLE_SPAWN_ATTEMPTS 5
void spawnTwinkleSpot() {
  for (int i = 0; i < TWINKLE_MAX_ACTIVE; i++) {
    if (!twinkleSpots[i].active) {
      uint8_t strip = random(3);
      uint8_t pos = random(stripLength(strip));

      // Try to find a position that isn't crowded.
      for (int attempt = 0; attempt < TWINKLE_SPAWN_ATTEMPTS; attempt++) {
        if (!isTooCloseToActiveSpot(strip, pos)) break;
        strip = random(3);
        pos = random(stripLength(strip));
      }
      // If we still collide after all attempts, spawn anyway — it's
      // better than visibly dropping spawns.

      twinkleSpots[i].strip = strip;
      twinkleSpots[i].position = pos;
      if (twinkleMonochrome) {
        twinkleSpots[i].hue = twinkleMonoHue;
      } else {
        uint8_t paletteIndex = random(TWINKLE_PALETTE_SIZE);
        twinkleSpots[i].hue = twinklePalette[paletteIndex];
        twinkleMonoHue = twinklePalette[paletteIndex];  // latch for mono toggle
      }
      twinkleSpots[i].birthMs = millis();
      twinkleSpots[i].active = true;
      return;
    }
  }
  // All slots full — skip this spawn.
}

// Refresh active twinkle spots: fade in over lifetime, spread to neighbors.
void updateTwinkleSpots() {
  unsigned long now = millis();
  for (int i = 0; i < TWINKLE_MAX_ACTIVE; i++) {
    if (!twinkleSpots[i].active) continue;

    unsigned long age = now - twinkleSpots[i].birthMs;
    if (age > TWINKLE_SPOT_LIFETIME) {
      twinkleSpots[i].active = false;
      continue;
    }

    // Brightness ramps from 0 → 255 over the first half, then holds at 255.
    float fraction = (float)age / TWINKLE_SPOT_LIFETIME;
    uint8_t brightness;
    if (fraction < 0.5) {
      brightness = (uint8_t)(255 * (fraction * 2.0));  // ramp up
    } else {
      brightness = 255;  // hold
    }

    CRGB color = CHSV(twinkleSpots[i].hue, 255, brightness);
    uint8_t s = twinkleSpots[i].strip;
    int p = twinkleSpots[i].position;

    // Center pixel at full computed brightness.
    setStripPixel(s, p, color);

    // Neighbors at half brightness for a gentle spread.
    CRGB neighborColor = CHSV(twinkleSpots[i].hue, 255, brightness / 2);
    setStripPixel(s, p - 1, neighborColor);
    setStripPixel(s, p + 1, neighborColor);
  }
}

// Main Twinkle update — called every loop() when in Twinkle mode.
void updateTwinkle() {
  // Global fade: every LED dims a little each frame.
  fadeToBlackBy(ledsPin25, NUM_LEDS_PIN25, TWINKLE_FADE_AMOUNT);
  fadeToBlackBy(ledsPin17, NUM_LEDS_PIN17, TWINKLE_FADE_AMOUNT);
  fadeToBlackBy(ledsPin16, NUM_LEDS_PIN16, TWINKLE_FADE_AMOUNT);

  // Spawn new spots at the configured rate.
  unsigned long now = millis();
  if (now - millisOfLastTwinkleSpawn >= millisBetweenTwinkleSpawns) {
    millisOfLastTwinkleSpawn = now;
    spawnTwinkleSpot();
  }

  // Refresh active spots (overrides the fade for living spots).
  updateTwinkleSpots();

  FastLED.show();
}
#endif


/*****************************************************************************
 *                                                                           *
 * INPUT                                                                     *
 *                                                                           *
 ****************************************************************************/

int getSwitchPosition() {
  uint16_t input = analogRead(MODE_SWITCH_PIN);
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
int getDebouncedSwitchPosition() {
  int rawSwitchPosition = getSwitchPosition();
  if (rawSwitchPosition != candidateSwitchPosition) {
    candidateSwitchPosition = rawSwitchPosition;
    millisWhenCandidateChanged = millis();
  }
  int nextSwitchPosition = committedSwitchPosition;
  if (candidateSwitchPosition != committedSwitchPosition &&
      millis() - millisWhenCandidateChanged >= 50) {
    committedSwitchPosition = candidateSwitchPosition;
    nextSwitchPosition = committedSwitchPosition;
#if IS_FASTLED_ENABLED
    isLedDirty = true;
    if (nextSwitchPosition == RAINBOW_MODE_INDEX) {
      // Entering Rainbow: pick a random starting hue, reset state, and
      // clear the strips so colors grow outward from the left.
      rainbowHue = random(256);
      rainbowAutoCycle = true;
      millisOfLastRainbowShift = millis();
#if IS_TWIST_ENABLED
      rainbowTwistBaseline = twist.getCount();
#endif
      setAllLEDs(CRGB::Black);
    }
    if (nextSwitchPosition == TWINKLE_MODE_INDEX) {
      // Entering Twinkle: clear strips and reset all twinkle state.
      for (int i = 0; i < TWINKLE_MAX_ACTIVE; i++) {
        twinkleSpots[i].active = false;
      }
      twinkleMonochrome = false;
      millisBetweenTwinkleSpawns = TWINKLE_DEFAULT_SPAWN_MS;
      millisOfLastTwinkleSpawn = millis();
#if IS_TWIST_ENABLED
      twinkleTwistBaseline = twist.getCount();
#endif
      setAllLEDs(CRGB::Black);
    }
#endif
  }
  return nextSwitchPosition;
}


#if IS_TWIST_ENABLED
// Clamp the twist brightness window so the current position stays inside it.
void clampTwistWindow(int currentTwistPosition) {
  if (currentTwistPosition < twistBrightnessWindowMin) {
    twistBrightnessWindowMin = currentTwistPosition;
    twistBrightnessWindowMax = currentTwistPosition + twistBrightnessWindowSize;
  }
  if (currentTwistPosition > twistBrightnessWindowMax) {
    twistBrightnessWindowMin = currentTwistPosition - twistBrightnessWindowSize;
    twistBrightnessWindowMax = currentTwistPosition;
  }
}
#endif

/*****************************************************************************
 *                                                                           *
 * PRESENCE                                                                  *
 *                                                                           *
 ****************************************************************************/
#if IS_PRESENCE_ENABLED
int16_t checkPresence() {
  sths34pf80_tmos_drdy_status_t dataReady;
  mySensor.getDataReady(&dataReady);

  // Check whether sensor has new data - run through loop if data is ready
  if (dataReady.drdy == 1) {
    sths34pf80_tmos_func_status_t status;
    mySensor.getStatus(&status);

    // If presence flag is high, then print data
    if (status.pres_flag == 1) {
      // Presence Units: cm^-1
      mySensor.getPresenceValue(&presenceVal);
      Serial.print("Presence: ");
      Serial.print(presenceVal);
      Serial.println(" cm^-1");
      return presenceVal;
    }
  }

  return 0;
}

// In Night mode, fade brightness to zero after the presence timeout expires.
int applyNightFade(int brightness) {
  unsigned long millisSincePresence = millis() - millisOfLastPresenceDetection;
  if (millisSincePresence <= millisTimeoutAtNight) {
    return brightness;
  }
  if (millisSincePresence < millisTimeoutAtNight + millisFadeDuration) {
    float amountFadeComplete = 1.0 * (millisSincePresence - millisTimeoutAtNight) / millisFadeDuration;
    int brightnessReduction = (int)(255 * amountFadeComplete);
    return max(0, brightness - brightnessReduction);
  }
  return 0;
}
#endif // IS_PRESENCE_ENABLED


/*****************************************************************************
 *                                                                           *
 * WEATHER                                                                   *
 *                                                                           *
 ****************************************************************************/
#if IS_WIFI_ENABLED
void fetchWeatherReport() {
  // wait for WiFi connection
  Serial.println("about to try to use wifi");
  if ((wifiMulti.run() == WL_CONNECTED)) {


    HTTPClient http;
    http.begin(WEATHER_URL);
    Serial.print("Requesting ");
    Serial.println(WEATHER_URL);
    // start connection and send HTTP header
    int httpCode = http.GET();

    // httpCode will be negative on error
    millisWhenWeatherLastFetched = millis();
    if (httpCode > 0) {
      // HTTP header has been send and Server response header has been handled
      // file found at server
      if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        parseWeatherReport(payload);
        Serial.print("Weather report: ");
        Serial.print(millisWhenWeatherLastFetched);
        Serial.print(" - ");
        Serial.println(payload);
        
      }
    } else {
      USE_SERIAL.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
    }

    http.end();
  }
}

void parseWeatherReport(String raw) {
  for (int i = 0; i < WEATHER_REPORT_MAX_LENGTH; ++i) {
    weatherReport[i] = "";
  }

  int tokensFound = 0;
  int tokenStart = 0;
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
      String token = raw.substring(tokenStart,i);
      i += 1;
      tokenStart = i;
      if (tokensFound < WEATHER_REPORT_MAX_LENGTH) {
        weatherReport[tokensFound] = token;
        tokensFound += 1;
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
void reportAirQuality() {

  // some floats to store read values in
  float ambientHumidity41;
  float ambientTemperature41;
  float co2;
  float massConcentrationPm1p0;
  float massConcentrationPm2p5;
  float massConcentrationPm4p0;
  float massConcentrationPm10p0;
  float ambientHumidity55;
  float ambientTemperature55;
  float vocIndex;
  float noxIndex;

  // readMeasurement will return true when fresh data is available
  if (sen41.readMeasurement()) {
    ambientHumidity41 = sen41.getHumidity();
    ambientTemperature41 = sen41.getTemperature();
    co2 = sen41.getCO2();
  } else {
    Serial.print(F("."));
  }
  uint16_t error;
  char errorMessage[256];

  error = sen55.readMeasuredValues(
      massConcentrationPm1p0, massConcentrationPm2p5, massConcentrationPm4p0,
      massConcentrationPm10p0, ambientHumidity55, ambientTemperature55, vocIndex,
      noxIndex);

  if (error) {
      Serial.print("Error trying to execute readMeasuredValues(): ");
      errorToString(error, errorMessage, 256);
      Serial.println(errorMessage);
  }

  String airUrl = AIR_URL;
  airUrl += TEMP_41_PREFIX;
  airUrl += ambientTemperature41;
  airUrl += TEMP_55_PREFIX;
  airUrl += ambientTemperature55;
  airUrl += CO2_PREFIX;
  airUrl += co2;
  airUrl += HUMIDITY_41_PREFIX;
  airUrl += ambientHumidity41;
  airUrl += HUMIDITY_55_PREFIX;
  airUrl += ambientHumidity55;
  airUrl += PARTICULATE_1p0_PREFIX;
  airUrl += massConcentrationPm1p0;
  airUrl += PARTICULATE_2p5_PREFIX;
  airUrl += massConcentrationPm2p5;
  airUrl += PARTICULATE_4p0_PREFIX;
  airUrl += massConcentrationPm4p0;
  airUrl += PARTICULATE_10_PREFIX;
  airUrl += massConcentrationPm10p0;
  airUrl += VOC_PREFIX;
  airUrl += vocIndex;
  airUrl += NOX_PREFIX;
  airUrl += noxIndex;
  Serial.println(airUrl);
  sendAirReport(airUrl);
}

void sendAirReport(String airUrl) {
  // wait for WiFi connection
  if ((wifiMulti.run() == WL_CONNECTED)) {

    HTTPClient http;
    http.begin(airUrl);
    // start connection and send HTTP header
    int httpCode = http.GET();

    // httpCode will be negative on error
    if (httpCode > 0) {
      // HTTP header has been send and Server response header has been handled
      // file found at server
      if (httpCode == HTTP_CODE_OK) {
        millisWhenAirLastReported = millis();
        String payload = http.getString();
        Serial.println(payload);

      }
    } else {
      USE_SERIAL.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
    }

    http.end();
  }
}
#endif // IS_AIR_SENSOR_ENABLED && IS_WIFI_ENABLED
