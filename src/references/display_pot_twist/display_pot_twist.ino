/*
 OpenLCD is an LCD with Serial/I2C/SPI interfaces.
 By: Phil Light, based on code by Nathan Seidle

 Note: This code expects the display to be listening at the default I2C address. If your display is not at 0x72, you can
 do a hardware reset. Tie the RX pin to ground and power up OpenLCD. You should see the splash screen
 then "System reset Power cycle me" and the backlight will begin to blink. Now power down OpenLCD and remove
 the RX/GND jumper. OpenLCD is now reset.
 To get this code to work, attached an OpenLCD to an Arduino Uno using the following pins:
 SCL (OpenLCD) to A5 (Arduino)
 SDA to A4
 VIN to 5V
 GND to GND
 Command cheat sheet:
 ASCII / DEC / HEX
 '|'    / 124 / 0x7C - Put into setting mode
 Ctrl+c / 3 / 0x03 - Change width to 20
 Ctrl+d / 4 / 0x04 - Change width to 16
 Ctrl+e / 5 / 0x05 - Change lines to 4
 Ctrl+f / 6 / 0x06 - Change lines to 2
 Ctrl+g / 7 / 0x07 - Change lines to 1
 Ctrl+h / 8 / 0x08 - Software reset of the system
 Ctrl+i / 9 / 0x09 - Enable/disable splash screen
 Ctrl+j / 10 / 0x0A - Save currently displayed text as splash
 Ctrl+k / 11 / 0x0B - Change baud to 2400bps
 Ctrl+l / 12 / 0x0C - Change baud to 4800bps
 Ctrl+m / 13 / 0x0D - Change baud to 9600bps
 Ctrl+n / 14 / 0x0E - Change baud to 14400bps
 Ctrl+o / 15 / 0x0F - Change baud to 19200bps
 Ctrl+p / 16 / 0x10 - Change baud to 38400bps
 Ctrl+q / 17 / 0x11 - Change baud to 57600bps
 Ctrl+r / 18 / 0x12 - Change baud to 115200bps
 Ctrl+s / 19 / 0x13 - Change baud to 230400bps
 Ctrl+t / 20 / 0x14 - Change baud to 460800bps
 Ctrl+u / 21 / 0x15 - Change baud to 921600bps
 Ctrl+v / 22 / 0x16 - Change baud to 1000000bps
 Ctrl+w / 23 / 0x17 - Change baud to 1200bps
 Ctrl+x / 24 / 0x18 - Change the contrast. Follow Ctrl+x with number 0 to 255. 120 is default.
 Ctrl+y / 25 / 0x19 - Change the TWI address. Follow Ctrl+x with number 0 to 255. 114 (0x72) is default.
 Ctrl+z / 26 / 0x1A - Enable/disable ignore RX pin on startup (ignore emergency reset)
 '-'    / 45 / 0x2D - Clear display. Move cursor to home position.
        / 128-157 / 0x80-0x9D - Set the primary backlight brightness. 128 = Off, 157 = 100%.
        / 158-187 / 0x9E-0xBB - Set the green backlight brightness. 158 = Off, 187 = 100%.
        / 188-217 / 0xBC-0xD9 - Set the blue backlight brightness. 188 = Off, 217 = 100%.
 For example, to change the baud rate to 115200 send 124 followed by 18.
*/

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

void setup()
{
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


void loop()
{

//  Serial.println(twistCount);

  if (twist.isPressed()){
    Serial.println(" Pressed!");
    twistStartCount = twist.getCount();
  }
  updateTwistColor();

  uint16_t switchPos = getSwitchPosition();

  i2cSendValue(twist.getCount(), switchPos); //Send the four characters to the display

  

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

  Wire.print("Switch: ");
  Wire.print(switchPos);
  Wire.print("       ");
  Wire.print("Twist: ");
  Wire.print(value);


  Wire.endTransmission(); //Stop I2C transmission
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
  } else if(input < 400){
    // position 1 is about 270
    val = 1;
  } else if(input < 900){
    // position 2 is about 710
    val = 2;
  } else if(input < 1300){
    // position 3 is about 1150
    val = 3;
  } else if(input < 1800){
    // position 4 is about 1610
    val = 4;
  } else if(input < 2200){
    // position 5 is about 2050
    val = 5;
  } else if(input < 2700){
    // position 6 is about 2500
    val = 6;
  } else if(input < 3200){
    // position 7 is about 2950
    val = 7;
  } else if(input < 3900){
    // position 8 is about 3650
    val = 8;
  } else {
    // position 9 is always 4095
    val = 9;
  }

  return val;
}
