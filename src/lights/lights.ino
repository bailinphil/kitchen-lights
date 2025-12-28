// This project uses a half-dozen different libraries and code which derives
// from the examples which come from them. See licenses.h for details.
#include "licenses.h"
#include <Arduino.h>

/*
 * WiFi
 */

#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#define USE_SERIAL Serial
WiFiMulti wifiMulti;
#include "network_credentials.h"
#define AP_SSID  "Kitchen_Lights"
unsigned long millisWhenWeatherLastFetched = 0;
unsigned long millisWhenAirLastReported = 0;


/*
 * LED Strip
 */
// Display the lights.
#include <FastLED.h>

// Information about the LED strip itself
#define NUM_LEDS    100
#define CHIPSET     WS2811
#define COLOR_ORDER BRG
CRGB leds[NUM_LEDS];
CRGB color = CRGB::Red;
#define BRIGHTNESS  20

int latestSwitchPosition = -1;
int earlierSwitchPosition = -1;

/*
 * Twist
 */
// The Twist is used to allow user to control some details of the
// current lighting mode.
#include "SparkFun_Qwiic_Twist_Arduino_Library.h"
TWIST twist;

// Wire.h is used to write to the 16x2 display which gives the user
// information about what's going on with the lights, as well as
// information about air quality in the room.
#include <Wire.h>
#define DISPLAY_ADDRESS1 0x72 //This is the default address of the OpenLCD

#define twistBrightnessWindowSize 20
int twistBrightnessWindowCenter = 0;
int twistBrightnessWindowMin = - (twistBrightnessWindowSize / 2);
int twistBrightnessWindowMax = (twistBrightnessWindowSize / 2);

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

String modeName[] = {
"Standby"
,"Routine"
,"Cook Day"
,"Cook Night"
,"Dishes"
,"Night"
,"Sparkles"
,"Racetrack"
,"Twinkle"
,"Away"
};

CRGB modeColor[] = {CRGB::Black,
CRGB::White,
CRGB::Green,
CRGB::Blue,
CRGB::Orange,
CRGB::Red,
CRGB::BlueViolet,
CRGB::Goldenrod,
CRGB::ForestGreen,
CRGB::DimGray,
CRGB::Black};

/*
 * Presence
 */
#include "SparkFun_STHS34PF80_Arduino_Library.h"
STHS34PF80_I2C mySensor;

// Values to fill with presence and motion data
int16_t presenceVal = 0;
int16_t motionVal = 0;
float temperatureVal = 0;
unsigned long millisOfLastPresenceDetection = 0;
#define millisTimeoutAtNight 20000
#define millisFadeDuration 3000

/*
 * Air Quality: Particles, Humidity, Temp, voc, NOx
 */
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




/*****************************************************************************
 *                                                                           *
 * SETUP FUNCTIONS                                                           *
 *                                                                           *
 ****************************************************************************/

void setup() {
  Serial.begin(115200); // communication speed for debug messages
  setupLEDs();
  setup16x2();
  setupTwist();
  setupPresence();
  setupWiFi();
  setupSensiron41();
  setupSensiron55();
}

void setupLEDs(){
  delay( 3000 ); // power-up safety delay
  // It's important to set the color correction for your LED strip here,
  // so that colors can be more accurately rendered through the 'temperature' profiles
  FastLED.addLeds<CHIPSET, 3, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.addLeds<CHIPSET, 1, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.addLeds<CHIPSET, 4, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness( BRIGHTNESS );
}

void setup16x2(){
  Wire.begin(); //Join the bus as master
  //Send the reset command to the display - this forces the cursor to return to the beginning of the display
  Wire.beginTransmission(DISPLAY_ADDRESS1);
  Wire.write('|'); // Put LCD into setting mode
  Wire.write(160); // turn down the green of the backlight
  Wire.write(188); // turn off the blue of the backlight
  Wire.write('-'); // Send clear display command
  Wire.endTransmission();

  for(int i=0; i<WEATHER_REPORT_MAX_LENGTH; ++i){
    weatherReport[i] = "";
  }
}

void setupTwist(){
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

// https://github.com/sparkfun/SparkFun_STHS34PF80_Arduino_Library
void setupPresence(){
    // Establish communication with device
    if(mySensor.begin() == false)
    {
      Serial.println("Error setting up device - please check wiring.");
      while(1);
    }
}

void setupWiFi(){
  wifiMulti.addAP(STA_SSID, STA_PASS);
  millisWhenWeatherLastFetched = millis();
}

void setupSensiron41(){
  //sen41.enableDebugging(); // Uncomment this line to get helpful debug messages on Serial

  //.begin will start periodic measurements for us (see the later examples for details on how to override this)
  if (sen41.begin() == false)
  {
    Serial.println(F("Sensor not detected. Please check wiring. Freezing..."));
    while (1);
  }
}

void setupSensiron55(){
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
#endif

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
    // “SEN5x – Temperature Compensation Instruction” at www.sensirion.com.
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

/*****************************************************************************
 *                                                                           *
 * LOOP FUNCTIONS                                                            *
 *                                                                           *
 ****************************************************************************/

void loop()
{
  unsigned long millisSinceAirReport = millis() - millisWhenAirLastReported;
  if(millisSinceAirReport > 120500){
    loopAirQuality();
    millisWhenAirLastReported = millis();

  }

  
  unsigned long millisSinceWeatherFetch = millis() - millisWhenWeatherLastFetched;
  if(millisSinceWeatherFetch > 30000){
    fetchWeatherReport();
  }
  
  if (twist.isPressed()){
    twistBrightnessWindowCenter = twist.getCount();
    twistBrightnessWindowMin = twist.getCount() - twistBrightnessWindowSize;
    twistBrightnessWindowMax = twist.getCount() + twistBrightnessWindowSize;
    
  }

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
  if(currentSwitchPosition != earlierSwitchPosition && latestSwitchPosition == currentSwitchPosition){
    nextSwitchPosition = currentSwitchPosition;
  }
  earlierSwitchPosition = latestSwitchPosition;
  latestSwitchPosition = currentSwitchPosition;

  int presenceVal = checkPresence();
  if(presenceVal != 0){
    millisOfLastPresenceDetection = millis();
  }

  int currentTwistPosition = twist.getCount();
  if(currentTwistPosition < twistBrightnessWindowMin){
    twistBrightnessWindowMin = currentTwistPosition;
    twistBrightnessWindowMax = currentTwistPosition + twistBrightnessWindowSize;
  }
  if(currentTwistPosition > twistBrightnessWindowMax){
    twistBrightnessWindowMin = currentTwistPosition - twistBrightnessWindowSize;
    twistBrightnessWindowMax = currentTwistPosition;
  }
  float twistedPositionInWindow = (currentTwistPosition - twistBrightnessWindowMin) / (1.0 * twistBrightnessWindowSize);
  int requestedBrightness = (int) (255 * twistedPositionInWindow);

  // in the night, turn off the lights when we haven't seen someone in front of
  // the sensor for a while.
  if(nextSwitchPosition == 5){
    unsigned long millisSincePresence = millis() - millisOfLastPresenceDetection;
    if(millisSincePresence > millisTimeoutAtNight){
      if(millisSincePresence < millisTimeoutAtNight + millisFadeDuration){
        // fade out
        float amountFadeComplete = 1.0 * (millisSincePresence - millisTimeoutAtNight) / millisFadeDuration;
        int brightnessReduction = (int) (255 * amountFadeComplete);
        requestedBrightness = max(0,requestedBrightness - brightnessReduction);
      }
      else {
        requestedBrightness = 0;
      }
    }
  }

  FastLED.setBrightness(requestedBrightness);

  messageTop = prepareTopMessage(nextSwitchPosition);
  messageBottom = prepareBottomMessage();
  if(isDisplayDirty){
    i2cSendValue(messageTop, messageBottom);
  }

  fill(modeColor[nextSwitchPosition]);
  FastLED.show();

  delay(2);
}

void i2cSendValue(String messageTop, String messageBottom)
{
  Serial.println(messageTop);
  Serial.println(messageBottom);
  Wire.beginTransmission(DISPLAY_ADDRESS1); // transmit to device #1

  Wire.write('|'); //Put LCD into setting mode
  Wire.write('-'); //Send clear display command

  Wire.print(messageTop);
  unsigned int charsPrinted = messageTop.length();
  while( charsPrinted < 16 ){
    Wire.print(" ");
    charsPrinted += 1;
  }
  Wire.print(messageBottom);

  Wire.endTransmission(); //Stop I2C transmission
  isDisplayDirty = false;
  Serial.println("--------------");
}

void fill(CRGB color) {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = color;
  }
}

String prepareTopMessage(uint8_t switchPos){
  String result = weatherReport[0] + " " + modeName[switchPos];
  if(!result.equals(previousMessageTop)){
    isDisplayDirty = true;
    previousMessageTop = result;
  }
  return result;
}

String prepareBottomMessage(){
  if(currentWeatherReportDisplay > WEATHER_REPORT_MAX_LENGTH ||
      weatherReport[currentWeatherReportDisplay].length() == 0){
    currentWeatherReportDisplay = 1;
  }
  String result = weatherReport[currentWeatherReportDisplay];
  if(millis() - millisWhenBottomRowUpdated > 3000){
    millisWhenBottomRowUpdated = millis();
    currentWeatherReportDisplay += 1;
    isDisplayDirty = true;
  }
  return result;
}

int getSwitchPosition()
{

  uint16_t input;
  uint8_t val;
    // read the ADC
  input = analogRead(A0);

  // Serial.println(input);
  if(input < 100){
    // position 0 always reads 0
    val = 0;
    color = CRGB::Black;
  } else if(input < 400){
    // position 1 is about 270
    val = 1;
    color = CRGB::White;
  } else if(input < 900){
    // position 2 is about 710
    color = CRGB::Red;
    val = 2;
  } else if(input < 1300){
    // position 3 is about 1150
    color = CRGB::Green;
    val = 3;
  } else if(input < 1800){
    // position 4 is about 1610
    color = CRGB::Blue;
    val = 4;
  } else if(input < 2300){
    // position 5 is about 2050
    color = CRGB::Red;
    val = 5;
  } else if(input < 2700){
    // position 6 is about 2500
    color = CRGB::Yellow;
    val = 6;
  } else if(input < 3200){
    // position 7 is about 2950
    color = CRGB::White;
    val = 7;
  } else if(input < 3900){
    // position 8 is about 3650
    color = CRGB::White;
    val = 8;
  } else {
    // position 9 is always 4095
    color = CRGB::Black;
    val = 9;
  }

  return val;
}

void fetchWeatherReport() {
  // wait for WiFi connection
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
        Serial.println(payload);
        millisWhenWeatherLastFetched = millis();
      }
    } else {
      USE_SERIAL.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
    }

    http.end();
  }
}

void parseWeatherReport(String raw){
  for(int i = 0; i < WEATHER_REPORT_MAX_LENGTH; ++i){
    weatherReport[i] = "";
  }

  int tokensFound = 0;
  int tokenStart = 0;
  for(int i = 0; i < raw.length(); ++i){
    // expressing the degree symbol seems complex. This method seems to work for me:
    // https://forum.arduino.cc/t/solved-how-to-print-the-degree-symbol-extended-ascii/438685/40
    // but I don't yet know how to put that character into my text file. So instead,
    // in the text file on the server I'm outputting ^ character where ° should go.
    // This little check swaps the ^ for a character which appears as a degree symbol on
    // my display.
    if(raw.charAt(i) == '^'){
      raw.setCharAt(i, char(223));
    }

    // Use the | character as a delimiter to mark what info should be
    if(raw.charAt(i) == '|'){
      String token = raw.substring(tokenStart,i);
      i += 1;
      tokenStart = i;
      weatherReport[tokensFound] = token;
      tokensFound += 1;
    }
  }
}

int16_t checkPresence(){
  sths34pf80_tmos_drdy_status_t dataReady;
  mySensor.getDataReady(&dataReady);
  int16_t result = 0;

  // Check whether sensor has new data - run through loop if data is ready
  if(dataReady.drdy == 1)
  {
    sths34pf80_tmos_func_status_t status;
    mySensor.getStatus(&status);

    // If presence flag is high, then print data
    if(status.pres_flag == 1)
    {
      // Presence Units: cm^-1
      mySensor.getPresenceValue(&presenceVal);
      result = presenceVal;
      Serial.print("Presence: ");
      Serial.print(presenceVal);
      Serial.println(" cm^-1");
    }
  }

  return result;
}

void loopAirQuality(){

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

  if (sen41.readMeasurement()) // readMeasurement will return true when fresh data is available
  {
    /*
    Serial.println();

    Serial.print(F("CO2(ppm):"));
    Serial.print(sen41.getCO2());

    Serial.print(F("\tTemperature(C):"));
    Serial.print(sen41.getTemperature(), 1);

    Serial.print(F("\tHumidity(%RH):"));
    Serial.print(sen41.getHumidity(), 1);

    Serial.println();
    */
    ambientHumidity41 = sen41.getHumidity();
    ambientTemperature41 = sen41.getTemperature();
    co2 = sen41.getCO2();
  }
  else
  {
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
  } else {
      Serial.print("MassConcentrationPm1p0:");
      Serial.print(massConcentrationPm1p0);
      Serial.print("\t");
      Serial.print("MassConcentrationPm2p5:");
      Serial.print(massConcentrationPm2p5);
      Serial.print("\t");
      Serial.print("MassConcentrationPm4p0:");
      Serial.print(massConcentrationPm4p0);
      Serial.print("\t");
      Serial.print("MassConcentrationPm10p0:");
      Serial.print(massConcentrationPm10p0);
      Serial.print("\t");
      Serial.print("AmbientHumidity:");
      if (isnan(ambientHumidity55)) {
          Serial.print("n/a");
      } else {
          Serial.print(ambientHumidity55);
      }
      Serial.print("\t");
      Serial.print("AmbientTemperature:");
      if (isnan(ambientTemperature55)) {
          Serial.print("n/a");
      } else {
          Serial.print(ambientTemperature55);
      }
      Serial.print("\t");
      Serial.print("VocIndex:");
      if (isnan(vocIndex)) {
          Serial.print("n/a");
      } else {
          Serial.print(vocIndex);
      }
      Serial.print("\t");
      Serial.print("NoxIndex:");
      if (isnan(noxIndex)) {
          Serial.println("n/a");
      } else {
          Serial.println(noxIndex);
      }
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

/**************************
 * Sensiron SEN55
 **************************/

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



