/*!
 * @file Commomlcon.ino
 * @brief Display of several common icons supported in U8G2
 * @n U8G2 supports multiple sizes of icons, this demo selects several for display
 * @n U8G2 GitHub Link：https://github.com/olikraus/u8g2/wiki/fntlistall
 * 
 * @copyright  Copyright (c) 2010 DFRobot Co.Ltd (http://www.dfrobot.com)
 * @licence     The MIT License (MIT)
 * @author [Ajax](Ajax.zhong@dfrobot.com)
 * @version  V1.0
 * @date  2019-11-29
 * @get from https://www.dfrobot.com
 * @url https://github.com/DFRobot/
*/

#include <Arduino.h>
#include <U8g2lib.h>

#include <Wire.h>

/*
 *IIC Constructor
 *@param  rotation：    U8G2_R0 No rotation, horizontal, draw from left to right
                        U8G2_R1 Rotate 90 degrees clockwise, draw from top to  bottom
                        U8G2_R2 Rotate 180 degrees clockwise, draw from right to left 
                        U8G2_R3 Rotate 270 degrees clockwise, draw from bottom to top.
                        U8G2_MIRROR Display image content normally（v2.6.x and above)   Note: U8G2_MIRROR needs to be used with setFlipMode（）.
 *@param reset：U8x8_PIN_NONE Empty pin, reset pin will not be used.
 *
*/
U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);  //  M0/ESP32/ESP8266/mega2560/Uno/Leonardo


void setup(void){
  u8g2.begin();   //init
  u8g2.setFontPosTop();   /**When you use drawStr to display strings, the default criteria is to display the lower-left
  * coordinates of the characters.The function can be understood as changing the coordinate position to the upper left
  * corner of the display string as the coordinate standard.*/
    }


void loop(void){
	/*
       * firstPage will change the current page number position to 0
       * When modifications are between firstpage and nextPage, they will be re-rendered at each time.
       * This method consumes less ram space than sendBuffer
   */ 
   u8g2.firstPage();
   for( int i = 64 ;i < 287; i += 3){
     u8g2.clear();
     do {
       u8g2.setFont(u8g2_font_open_iconic_all_4x_t);  //Set the font to "u8g2_font_open_iconic_all_4x_t"
       /*
       * Draw a single character. The character is placed at the specified pixel posion x and y. 
	   * U8g2 supports the lower 16 bit of the unicode character range (plane 0/Basic Multilingual Plane): 
	   * The encoding can be any value from 0 to 65535. The glyph can be drawn only, if the encoding exists in the active font.
      */
	   u8g2.drawGlyph(/* x=*/0, /* y=*/0, /* encoding=*/i);    
       u8g2.drawGlyph(/* x=*/40, /* y=*/0, /* encoding=*/i+1); 
       u8g2.drawGlyph(/* x=*/80, /* y=*/0, /* encoding=*/i+2); 
     } while ( u8g2.nextPage());
    
    delay(2000);
  }
}
