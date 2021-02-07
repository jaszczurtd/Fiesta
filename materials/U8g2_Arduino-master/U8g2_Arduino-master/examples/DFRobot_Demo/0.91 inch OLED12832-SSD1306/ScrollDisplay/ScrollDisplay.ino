/*

  ScrollDisplay.ino

  Universal 8bit Graphics Library (https://github.com/olikraus/u8g2/)

  Copyright (c) 2016, olikraus@gmail.com
  All rights reserved.

  Modify by [Ajax](Ajax.zhong@dfrobot.com)
  url https://github.com/DFRobot/U8g2_Arduino
  2019-11-29 

*/

#include <Arduino.h>
#include <U8g2lib.h>

#include <Wire.h>

/*
  U8g2lib Example Overview:
    Frame Buffer Examples: clearBuffer/sendBuffer. Fast, but may not work with all Arduino boards because of RAM consumption
    Page Buffer Examples: firstPage/nextPage. Less RAM usage, should work with all Arduino boards.
    U8x8 Text Only Example: No RAM usage, direct communication with display controller. No graphics, 8x8 Text only.
    
*/

// Please UNCOMMENT one of the contructor lines below
// U8g2 Contructor List (Frame Buffer)
// The complete list is available here: https://github.com/olikraus/u8g2/wiki/u8g2setupcpp
// Please update the pin numbers according to your setup. Use U8X8_PIN_NONE if the reset pin is not connected

U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE); //  M0/ESP32/ESP8266/mega2560/Uno/Leonardo

int16_t offset;				// current offset for the scrolling text
u8g2_uint_t width;			// pixel width of the scrolling text (must be lesser than 128 unless U8G2_16BIT is defined
const char *text = "U8g2";	// scroll this text from right to left


void setup(void) {
  u8g2.begin();
    
  u8g2.setFont(u8g2_font_courB18_tr);	// set the target font for the text width calculation
  width = u8g2.getUTF8Width(text);		// calculate the pixel width of the text
  offset = width+128;
  
  
}

void loop(void) {
  u8g2.clearBuffer();						// clear the complete internal memory
  u8g2.drawFrame(/* x=*/0, /* y=*/0, /* w=*/128, /* h=*/32);     // draw the scrolling text at current offset
  u8g2.setFont(u8g2_font_courB18_tr);		// set the target font
  u8g2.drawUTF8(-width+offset,23, text);		// draw the scolling text
  
  u8g2.sendBuffer();   
  offset--;								// scroll by one pixel
  if ( offset == 0 )	
    offset = width+128;			// start over again
    
  delay(10);							// do some small delay
}
