/*
  Read and interact with the SparkFun Qwiic Twist digital RGB encoder
  By: Nathan Seidle
  SparkFun Electronics
  Date: December 3rd, 2018
  License: MIT. See license file for more information but you can
  basically do whatever you want with this code.

  This example prints the number of steps the encoder has been twisted.
  
  Feel like supporting open source hardware?
  Buy a board from SparkFun! https://www.sparkfun.com/products/15083

  Hardware Connections:
  Plug a Qwiic cable into the Qwiic Twist and a BlackBoard
  If you don't have a platform with a Qwiic connection use the SparkFun Qwiic Breadboard Jumper (https://www.sparkfun.com/products/14425)
  Open the serial monitor at 115200 baud to see the output
*/

#include "SparkFun_Qwiic_Twist_Arduino_Library.h" //Click here to get the library: http://librarymanager/All#SparkFun_Twist
TWIST twist;                                      //Create instance of this object

#include <Wire.h>

#define DISPLAY_ADDRESS1 0x72 

void setup()
{
  Serial.begin(9600);
  Serial.println("Qwiic Twist Example");

  if (twist.begin() == false)
  {
    Serial.println("Twist does not appear to be connected. Please check wiring. Freezing...");
    while (1)
      ;
  }

  //Send the reset command to the display - this forces the cursor to return to the beginning of the display
  Wire.beginTransmission(DISPLAY_ADDRESS1);
  Wire.write('|'); //Put LCD into setting mode
  //Wire.write(150);
  Wire.write(160); // turn down the green of the backlight
  Wire.write(188); // turn off the blue of the backlight
  Wire.write('-'); //Send clear display command
  Wire.endTransmission();


}

void loop()
{
  //Serial.print("Count: ");
  int twistCount = twist.getCount();
  Serial.println(twistCount);

  if (twist.isPressed())
    Serial.println(" Pressed!");

  Wire.beginTransmission(DISPLAY_ADDRESS1); // transmit to device #1


  Wire.write('|'); //Put LCD into setting mode
  //Wire.write(150);
  //Wire.write(158);
  //Wire.write(188);
  Wire.write('-'); //Send clear display command

  Wire.print("Switch: ");
  Wire.print(" none   ");
  Wire.print("Twist: ");
  Wire.print(twistCount);

  delay(10);
}
