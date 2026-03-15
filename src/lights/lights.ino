// This project uses a half-dozen different libraries and code which derives
// from the examples which come from them. See licenses.h for details.
#include "licenses.h"
#include <Arduino.h>

/*
 * Hardware Selection
 */
#define IS_AIR_SENSOR_ENABLED false
#define IS_TWIST_ENABLED      true
#define IS_PRESENCE_ENABLED   false
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

// Information about the LED strip itself
#define NUM_LEDS    100
#define CHIPSET     WS2811
#define COLOR_ORDER BRG
CRGB leds[NUM_LEDS];

CRGB previousColor;
int previousBrightness;
bool isLedDirty = true;
#define BRIGHTNESS  20
#endif //IS_FASTLED_ENABLED

/*
 * Mode Switching
 */
#define MODE_SWITCH_PIN A5
int latestSwitchPosition = -1;
int earlierSwitchPosition = -1;
String modeName[] = {
  "Standby",
  "Routine",
  "Cook Day",
  "Cook Night",
  "Dishes",
  "Night",
  "Sparkles",
  "Racetrack",
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
  CRGB::Goldenrod,   // Racetrack
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
  {  0, 255,   0},  // Racetrack
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
int millisWhenBottomRowUpdated = 0;
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
  // It's important to set the color correction for your LED strip here,
  // so that colors can be more accurately rendered through the 'temperature' profiles
  FastLED.addLeds<CHIPSET, 3, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.addLeds<CHIPSET, 1, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.addLeds<CHIPSET, 4, COLOR_ORDER>(leds, NUM_LEDS);
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
  Serial.print("try to connect to ");
  wifiMulti.addAP(STA_SSID_HOME, STA_PASS_HOME);
  wifiMulti.addAP(STA_SSID_WORK, STA_PASS_WORK);
  wifiMulti.addAP(STA_SSID_PHONE, STA_PASS_PHONE);
  wifiMulti.addAP(STA_SSID_PROTO, STA_PASS_PROTO);
  millisWhenWeatherLastFetched = millis();
  Serial.println("done!");
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

    // set a temperature offset in degrees celsius
    // Note: supported by SEN54 and SEN55 sensors
    // By default, the temperature and humidity outputs from the sensor
    // are compensated for the modules self-heating. If the module is
    // designed into a device, the temperature compensation might need
    // to be adapted to incorporate the change in thermal coupling and
    // self-heating of other device components.
    //
    // A guide to achieve optimal performance, including references
    // to mechanical design-in examples can be found in the app note
    // "SEN5x – Temperature Compensation Instruction" at www.sensirion.com.
    // Please refer to those application notes for further information
    // on the advanced compensation settings used
    // in `setTemperatureOffsetParameters`, `setWarmStartParameter` and
    // `setRhtAccelerationMode`.
    //
    // Adjust tempOffset to account for additional temperature offsets
    // exceeding the SEN module's self heating.
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
 * LOOP                                                                      *
 *                                                                           *
 ****************************************************************************/

void loop()
{
Serial.println(".");
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
  if (twist.isPressed()) {
    twistBrightnessWindowCenter = twist.getCount();
    twistBrightnessWindowMin = twist.getCount() - twistBrightnessWindowSize;
    twistBrightnessWindowMax = twist.getCount() + twistBrightnessWindowSize;
  }
#endif

  int currentSwitchPosition = getSwitchPosition();
  // sometimes the analog readings of the switch "flicker" out of range for a moment.
  // account for this by looking at the two previous readings and the current one.
  // of the three of those, one position should be present in two out of three
  // readings. That's the one we want to use.
  //
  // earlier latest current => next
  // A A A => A
  // A A B => A
  // A B A => A
  // A B B => B
  // B A A => A
  // B A B => B
  // B B A => B
  // B B B => B
  int nextSwitchPosition = earlierSwitchPosition;
  if (currentSwitchPosition != earlierSwitchPosition && latestSwitchPosition == currentSwitchPosition) {
    nextSwitchPosition = currentSwitchPosition;
  }
  isLedDirty = isLedDirty && (nextSwitchPosition != currentSwitchPosition);
  earlierSwitchPosition = latestSwitchPosition;
  latestSwitchPosition = currentSwitchPosition;

#if IS_PRESENCE_ENABLED
  int detectedPresence = checkPresence();
  if (detectedPresence != 0) {
    millisOfLastPresenceDetection = millis();
  }
#endif

#if IS_TWIST_ENABLED
  int currentTwistPosition = twist.getCount();
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
    isLedDirty = true;
  }
#endif // IS_FASTLED_ENABLED
  uint8_t* tc = twist_colors[currentSwitchPosition];
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
  if(isLedDirty){
#if IS_TWIST_ENABLED
    previousBrightness = requestedBrightness;
#endif // IS_TWIST_ENABLED
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
  if (currentWeatherReportDisplay > WEATHER_REPORT_MAX_LENGTH ||
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

  Wire.endTransmission(); //Stop I2C transmission
  isDisplayDirty = false;
  Serial.println("--------------");
}
#endif //IS_DISPLAY_ENABLED

/*****************************************************************************
 *                                                                           *
 * LEDS                                                                      *
 *                                                                           *
 ****************************************************************************/

#if IS_FASTLED_ENABLED
void setAllLEDs(CRGB color) {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = color;
  }
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
  Serial.println("about to ask for wifi");
  if ((wifiMulti.run() == WL_CONNECTED)) {


    HTTPClient http;
    http.begin(WEATHER_URL);
    Serial.println(WEATHER_URL);
    // start connection and send HTTP header
    int httpCode = http.GET();

    // httpCode will be negative on error
    if (httpCode > 0) {
      // HTTP header has been send and Server response header has been handled
      // file found at server
      if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        parseWeatherReport(payload);
        //Serial.println(payload);
        millisWhenWeatherLastFetched = millis();
      }
    } else {
      USE_SERIAL.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
    }

    http.end();
  }
}

void parseWeatherReport(String raw) {
  Serial.print("parsing ");
  Serial.println(raw);
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
      weatherReport[tokensFound] = token;
      tokensFound += 1;
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
