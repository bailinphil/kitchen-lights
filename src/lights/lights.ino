// This project uses a half-dozen different libraries and code which derives 
// from the examples which come from them. See licenses.h for details.
#include "licenses.h"

/*
 * LED Strip
 */
// Display the lights.
#include <FastLED.h>

// Information about the LED strip itself
#define NUM_LEDS    20
#define CHIPSET     WS2811
#define COLOR_ORDER BRG
CRGB leds[NUM_LEDS];
CRGB color = CRGB::Red;
#define BRIGHTNESS  20

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

int twistStartCount = 0;

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

String modeName[] = {"Standby    "
,"Routine    "
,"Cook Day   "
,"Cook Night "
,"Dishes     "
,"Night      "
,"Sparkles   "
,"Racetrack  "
,"Twinkle    "
,"Away       "
};

// make some custom characters:
byte degree[8] = {
  0b11100,
  0b10100,
  0b11100,
  0b00000,
  0b00000,
  0b00000,
  0b00000,
  0b00000
};

/*
 * WiFi
 */
#include <Arduino.h>

#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#define USE_SERIAL Serial
WiFiMulti wifiMulti;
#include "network_credentials.h"
#define AP_SSID  "Kitchen_Lights"
int millisWhenWeatherLastFetched = 0;

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
  setupWiFi();
}

void setupLEDs(){
  delay( 3000 ); // power-up safety delay
  // It's important to set the color correction for your LED strip here,
  // so that colors can be more accurately rendered through the 'temperature' profiles
  FastLED.addLeds<CHIPSET, 3, COLOR_ORDER>(leds, NUM_LEDS);
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

  twistStartCount = twist.getCount();
  twist.setColor(100,10,0);
}

void setupWiFi(){
  wifiMulti.addAP(STA_SSID, STA_PASS);
  millisWhenWeatherLastFetched = millis();
}

/*****************************************************************************
 *                                                                           *
 * LOOP FUNCTIONS                                                            *
 *                                                                           *
 ****************************************************************************/

void loop()
{
  int millisSinceWeatherFetch = millis() - millisWhenWeatherLastFetched;
  if(millisSinceWeatherFetch > 10000){
    fetchWeatherReport();
  }

  // why is this code here? what does it do?
  /*while (Serial.available()) {
    Serial.write(Serial.read());
  }*/

  if (twist.isPressed()){
    twistStartCount = twist.getCount();
  }

  int twistsSincePress = twist.getCount() - twistStartCount;
  int requestedBrightness = min(max(BRIGHTNESS + twistsSincePress * 8, 0),255);
  FastLED.setBrightness(requestedBrightness);

  

  messageTop = prepareTopMessage(getSwitchPosition());
  messageBottom = prepareBottomMessage();
  if(isDisplayDirty){
    i2cSendValue(messageTop, messageBottom);
  }

  fill(color);
  FastLED.show();  

  delay(10);
}

//Given a number, i2cSendValue chops up an integer into four values and sends them out over I2C
void i2cSendValue(String messageTop, String messageBottom)
{
  Wire.beginTransmission(DISPLAY_ADDRESS1); // transmit to device #1

  Wire.write('|'); //Put LCD into setting mode
  Wire.write('-'); //Send clear display command

  Wire.print(messageTop);
  Wire.print(messageBottom);

  Wire.endTransmission(); //Stop I2C transmission
  isDisplayDirty = false;
}

void fill(CRGB color) {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = color;
  }
}

String prepareTopMessage(uint8_t switchPos){
  String result = modeName[switchPos] + weatherReport[0];
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
  } else if(input < 2200){
    // position 5 is about 2050
    color = CRGB::Purple;
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
    // start connection and send HTTP header
    int httpCode = http.GET();

    // httpCode will be negative on error
    if (httpCode > 0) {
      // HTTP header has been send and Server response header has been handled
      // file found at server
      if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        parseWeatherReport(payload);
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

