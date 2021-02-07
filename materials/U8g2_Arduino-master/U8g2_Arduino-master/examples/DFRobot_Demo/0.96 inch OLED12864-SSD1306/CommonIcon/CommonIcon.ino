/*!
 * @file CommonIcon.ino
 * @brief Display of several common icons supported in U8G2
 * @n U8G2 supports multiple sizes of icons, this demo selects several for display
 * @n U8G2 GitHub Link：https://github.com/olikraus/u8g2/wiki/fntlistall
 * 
 * @copyright  Copyright (c) 2010 DFRobot Co.Ltd (http://www.dfrobot.com)
 * @licence     The MIT License (MIT)
 * @author [Ivey](Ivey.lu@dfrobot.com)
 * @version  V1.0
 * @date  2019-11-29
 * @get from https://www.dfrobot.com
 * @url https://github.com/DFRobot/U8g2_Arduino
*/

#include <Arduino.h>
#include <U8g2lib.h>

//#include <SPI.h>
#include <Wire.h>

/*
 * Display hardware IIC interface constructor
 *@param rotation：U8G2_R0 Not rotate, horizontally, draw direction from left to right
           U8G2_R1 Rotate clockwise 90 degrees, drawing direction from top to bottom
           U8G2_R2 Rotate 180 degrees clockwise, drawing in right-to-left directions
           U8G2_R3 Rotate clockwise 270 degrees, drawing direction from bottom to top
           U8G2_MIRROR Normal display of mirror content (v2.6.x version used above)
           Note: U8G2_MIRROR need to be used with setFlipMode().
 *@param reset：U8x8_PIN_NONE Indicates that the pin is empty and no reset pin is used
 * Display hardware SPI interface constructor
 *@param  Just connect the CS pin (pins are optional)
 *@param  Just connect the DC pin (pins are optional)
 *
*/
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(/* rotation=*/U8G2_R0, /* reset=*/ U8X8_PIN_NONE);    //  M0/ESP32/ESP8266/mega2560/Uno/Leonardo
//U8G2_SSD1306_128X64_NONAME_1_4W_HW_SPI u8g2(/* rotation=*/U8G2_R0, /* cs=*/ 10, /* dc=*/ 9);


void setup(void)
{
  u8g2.begin();  //init
  u8g2.setFontPosTop();  /**When you use drawStr to display strings, the default criteria is to display the lower-left
  * coordinates of the characters.The function can be understood as changing the coordinate position to the upper left
  * corner of the display string as the coordinate standard.*/
}


void loop(void)
{
	/*
       * firstPage will change the current page number position to 0
       * When modifications are between firstpage and nextPage, they will be re-rendered at each time.
       * This method consumes less ram space than sendBuffer
   */ 
   u8g2.firstPage();
   for(int i = 64 ; i <287; i++){
   u8g2.clear();
   do
   {
      u8g2.setFont(u8g2_font_open_iconic_all_4x_t );  //Set the font to "u8g2_font_open_iconic_all_4x_t"
      /*
       * Draw a single character. The character is placed at the specified pixel posion x and y. 
	   * U8g2 supports the lower 16 bit of the unicode character range (plane 0/Basic Multilingual Plane): 
	   * The encoding can be any value from 0 to 65535. The glyph can be drawn only, if the encoding exists in the active font.
      */
      u8g2.drawGlyph(/* x=*/0, /* y=*/16, /* encoding=*/i);  
      u8g2.drawGlyph(/* x=*/48, /* y=*/16, /* encoding=*/i+1);  
      u8g2.drawGlyph(/* x=*/96, /* y=*/16, /* encoding=*/i+2);  
   } while ( u8g2.nextPage() );
   i = i+3;
   delay(2000);
  }
}

