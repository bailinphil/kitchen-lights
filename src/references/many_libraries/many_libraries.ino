// Fundamentally, this project is about lighting a single room. FastLED is 
// used to drive the LED light strip we are using.
#include <FastLED.h>

// There are two controls for it. One is an indexed potentiometer which 
// we will read as an analog input. This will tell us the current mode
// we should be in. No additional library is needed.

// The second input is a SparkFun Twist, which is a rotary encoder that
// also has an LED in it. 
#include "SparkFun_Qwiic_Twist_Arduino_Library.h" 
TWIST twist;

// We will give the users of the system feedback on a 16x2 display. That
// is driven with this library.
#include <Wire.h>

// To tell what time it is in the real world, we will connect to the internet.
#include "WiFi.h"
#include "network_credentials.h"
#define AP_SSID  "esp32-v6"
static volatile bool wifi_connected = false;
NetworkUDP ntpClient;


// Sometimes the lights will dim when no one is around. This library allows
// us to read from a human presence sensor which uses infrared light.
#include "SparkFun_STHS34PF80_Arduino_Library.h"

// and also we will measure the air quality in the kitchen. This one is for
// CO2, Temperature, and Humidity.
#include "SparkFun_SCD4x_Arduino_Library.h"
SCD4x mySensor;

// this one is to measure the amount of particulates in the air.
#include <Arduino.h>
#include <SensirionI2CSen5x.h>

void setup() {
  setupWifi();

}

void loop() {
  // put your main code here, to run repeatedly:

}

/*
 *
 * WiFi Functions. All of these were copy/pasted from 
 * https://github.com/espressif/arduino-esp32/blob/master/libraries/WiFi/examples/WiFiIPv6/WiFiIPv6.ino
 * I believe that this usage fits within the rules of the LGPL-2.1 License
 * that the arduino-esp32 code is written under.
 * 
 */
void setupWifi(){
  Serial.begin(115200);
  WiFi.disconnect(true);
  WiFi.onEvent(WiFiEvent);  // Will call WiFiEvent() from another thread.
  WiFi.mode(WIFI_MODE_APSTA);
  //enable ap ipv6 here
  WiFi.softAPenableIPv6();
  WiFi.softAP(AP_SSID);
  //enable sta ipv6 here
  WiFi.enableIPv6();
  WiFi.begin(STA_SSID, STA_PASS);
}

// WARNING: WiFiEvent is called from a separate FreeRTOS task (thread)!
void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_AP_START:
      //can set ap hostname here
      WiFi.softAPsetHostname(AP_SSID);
      break;
    case ARDUINO_EVENT_WIFI_STA_START:
      //set sta hostname here
      WiFi.setHostname(AP_SSID);
      break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED: break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP6:
      Serial.print("STA IPv6: ");
      Serial.println(WiFi.linkLocalIPv6());
      break;
    case ARDUINO_EVENT_WIFI_AP_GOT_IP6:
      Serial.print("AP IPv6: ");
      Serial.println(WiFi.softAPlinkLocalIPv6());
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      wifiOnConnect();
      wifi_connected = true;
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      wifi_connected = false;
      wifiOnDisconnect();
      break;
    default: break;
  }
}

void wifiOnConnect() {
  Serial.println("STA Connected");
  Serial.print("STA IPv4: ");
  Serial.println(WiFi.localIP());

  ntpClient.begin(2390);
}

void wifiOnDisconnect() {
  Serial.println("STA Disconnected");
  delay(1000);
  WiFi.begin(STA_SSID, STA_PASS);
}

