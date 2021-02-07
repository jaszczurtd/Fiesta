/*!
 * @file Font.ino
 * @brief Show several U8G2-supported fonts
 * @n U8G2 supports multiple fonts, and this demo is just a few fonts to display
 * @n U8G2 Font GitHub Link：https://github.com/olikraus/u8g2/wiki/fntlistall
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
  u8g2.begin();           //Initialize the function
  u8g2.setFontPosTop();   /**When you use drawStr to display strings, the default criteria is to display the lower-left
  * coordinates of the characters.The function can be understood as changing the coordinate position to the upper left
  * corner of the display string as the coordinate standard.*/
    }

void loop(void){
  u8g2.clearBuffer();            // Clears all pixel in the memory frame buffer.  
  u8g2.setFont(u8g2_font_maniac_tr); //Set the font set, which is“u8g2_font_maniac_tr“
  u8g2.drawStr(/* x=*/0, /* y=*/5, "DFR123");  //Start drawing strings at the coordinates of x-0, y-5 “DFR123”
  u8g2.sendBuffer();          //Send the content of the memory frame buffer to the display.      
  
  delay(1000);
   
  u8g2.clearBuffer();     // Clears all pixel in the memory frame buffer.      
  
  u8g2.setFont(u8g2_font_secretaryhand_tf); 
  u8g2.drawStr(0, 0, "DFR123");    
  
  u8g2.setFont(u8g2_font_heavybottom_tr);
  u8g2.drawStr(70, 0, "DFR123");   
  
  u8g2.setFont(u8g2_font_fancypixels_tf);
  u8g2.drawStr(0, 20, "DFR123");  
  
  u8g2.setFont(u8g2_font_fancypixels_tf);
  u8g2.drawStr(70, 20, "DFR123");  
  
  u8g2.sendBuffer();               
  delay(1000); 
}
