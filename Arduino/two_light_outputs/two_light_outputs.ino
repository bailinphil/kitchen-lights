/// @file    ColorTemperature.ino
/// @brief   Demonstrates how to use @ref ColorTemperature based color correction
/// @example ColorTemperature.ino

#include <FastLED.h>


// Information about the LED strip itself
#define NUM_LEDS    20
#define CHIPSET     WS2811
#define COLOR_ORDER GRB
CRGB leds[NUM_LEDS];

#define BRIGHTNESS  128


#include "SparkFun_Qwiic_Twist_Arduino_Library.h" //Click here to get the library: http://librarymanager/All#SparkFun_Twist
TWIST twist;

#include <Wire.h>

#define DISPLAY_ADDRESS1 0x72 //This is the default address of the OpenLCD

int cycles = 0;
float twistFullRed = 256.0;
float twistFullGreen = 25.0;
float twistFullBlue = 0.0;
int twistStartCount = 0;
char debugStr[255];

CRGB color = CRGB::White;

void setup() {
  delay( 3000 ); // power-up safety delay
  // It's important to set the color correction for your LED strip here,
  // so that colors can be more accurately rendered through the 'temperature' profiles
  FastLED.addLeds<CHIPSET, 3>(leds, NUM_LEDS);
  FastLED.addLeds<CHIPSET, 4>(leds, NUM_LEDS);
  FastLED.setBrightness( BRIGHTNESS );




  Wire.begin(); //Join the bus as master

  //By default .begin() will set I2C SCL to Standard Speed mode of 100kHz
  //Wire.setClock(400000); //Optional - set I2C SCL to High Speed Mode of 400kHz

  Serial.begin(9600); //Start serial communication at 9600 for debug statements
  Serial.println("OpenLCD Example Code");

  //Send the reset command to the display - this forces the cursor to return to the beginning of the display
  Wire.beginTransmission(DISPLAY_ADDRESS1);
  Wire.write('|'); //Put LCD into setting mode
  //Wire.write(150);
  Wire.write(160); // turn down the green of the backlight
  Wire.write(188); // turn off the blue of the backlight
  Wire.write('-'); //Send clear display command
  Wire.endTransmission();

  //


  if (twist.begin() == false)
  {
    Serial.println("Twist does not appear to be connected. Please check wiring. Freezing...");
    while (1);
  }

  twistStartCount = twist.getCount();
  twist.setColor(100,10,0);
}



void loop()
{

  if (twist.isPressed()){
    Serial.println(" Pressed!");
    twistStartCount = twist.getCount();
  }
  //updateTwistColor();

  int twistsSincePress = twist.getCount() - twistStartCount;
  int requestedBrightness = min(max(BRIGHTNESS + twistsSincePress * 3, 0),255);
  FastLED.setBrightness(requestedBrightness);

  uint16_t switchPos = getSwitchPosition();

  i2cSendValue(requestedBrightness, switchPos); //Send the four characters to the display

  fill(color);
  FastLED.show();  

  delay(50); //The maximum update rate of OpenLCD is about 100Hz (10ms). A smaller delay will cause flicker
}

//Given a number, i2cSendValue chops up an integer into four values and sends them out over I2C
void i2cSendValue(int value, uint16_t switchPos)
{
  Wire.beginTransmission(DISPLAY_ADDRESS1); // transmit to device #1


  Wire.write('|'); //Put LCD into setting mode
  //Wire.write(150);
  //Wire.write(158);
  //Wire.write(188);
  Wire.write('-'); //Send clear display command

  Wire.print("Color: ");
  Wire.print(switchPos);
  Wire.print("        ");
  Wire.print("Bright: ");
  Wire.print(value);


  Wire.endTransmission(); //Stop I2C transmission
}

void fill(CRGB color) {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = color;
  }
}

/*
 * Tinkering around with setting the color of the dial based on how much it has been rotated.
 * This uses a "snapshot" of where the knob was at the start of the program, looks at how far it
 * has been turned since, and divides the number of clicks by 25 (which is just over 1 rotation.)
 * 
 * then we scale the color up to full orange based on how many revolutions we've done.
 */
void updateTwistColor(){
  int twistCount = twist.getCount();
  int twistsFromStart = twistCount - twistStartCount;
  float twistCompletion = max(min(twistsFromStart / 48.0,0.99),0.0);
  uint8_t amountRed = (uint8_t) (twistFullRed * twistCompletion);
  uint8_t amountGreen = (uint8_t) (twistFullGreen * twistCompletion);
  uint8_t amountBlue = (uint8_t) (twistFullBlue * twistCompletion);
  sprintf(debugStr, "%f %i %i %i", twistCompletion, amountRed, amountGreen, amountBlue);
  Serial.println(debugStr);
  twist.setColor(amountRed, amountGreen, amountBlue);
}

int getSwitchPosition()
{

  uint16_t input;
  uint16_t val;
    // read the ADC
  input = analogRead(A0);

  // Serial.println(input);
  if(input < 100){
    // position 0 always reads 0
    val = 0;
    color = CRGB::White;
  } else if(input < 400){
    // position 1 is about 270
    val = 1;
    color = CRGB::Orange;
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


