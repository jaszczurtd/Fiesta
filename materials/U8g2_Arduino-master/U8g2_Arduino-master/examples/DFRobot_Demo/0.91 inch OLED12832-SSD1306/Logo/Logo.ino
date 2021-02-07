/*!
 * @file Logo.ino
 * @brief A demo for displaying bitmap
 * @n U8G2 Font GitHub Link：https://github.com/olikraus/u8g2/wiki/fntlistall
 * 
 * @copyright  Copyright (c) 2010 DFRobot Co.Ltd (http://www.dfrobot.com)
 * @licence     The MIT License (MIT)
 * @author [Ajax](Ajax.zhong@dfrobot.com)
 * @version  V1.0
 * @date  2019-11-29
 * @get from https://www.dfrobot.com
 * @url https://github.com/DFRobot/U8g2_Arduino
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

// width:30,height:30
const unsigned char col[] U8X8_PROGMEM= {0x00,0xc0,0x00,0x00,0x00,0xe0,0x01,0x00,0x00,0xe0,0x01,0x00,0x00,0xc0,0x00,0x00,
                                         0x00,0xc0,0x00,0x00,0x00,0xc0,0x00,0x00,0x00,0xe0,0x01,0x00,0x00,0xf8,0x07,0x00,
                                         0x06,0xfe,0x1f,0x18,0x07,0xff,0x3f,0x38,0xdf,0xff,0xff,0x3e,0xfa,0xff,0xff,0x17,
                                         0xf0,0xff,0xff,0x03,0xe0,0xff,0xff,0x01,0xe0,0xff,0xff,0x01,0xe0,0xff,0xff,0x01,
                                         0xe0,0xff,0xff,0x01,0x20,0x00,0x00,0x01,0xa0,0xff,0x7f,0x01,0xa0,0x01,0x60,0x01,
                                         0x20,0x07,0x38,0x01,0xe0,0x0c,0xcc,0x01,0xe0,0x39,0xe7,0x01,0xe0,0xe7,0xf9,0x01,
                                         0xc0,0x1f,0xfe,0x00,0x80,0xff,0x7f,0x00,0x00,0xfe,0x1f,0x00,0x00,0xf8,0x07,0x00,
                                         0x00,0xe0,0x01,0x00,0x00,0xc0,0x00,0x00}; 

                                         
void setup(void) {
  u8g2.begin();  //init
}

 

void loop(void) {

  u8g2.clearBuffer(); // Clears all pixel in the memory frame buffer. 
  u8g2.drawXBMP( /* x=*/0 , /* y=*/0 , /* width=*/30 , /* height=*/30 , col );     //Draw a XBM Bitmap. Position (x,y) is the upper left corner of the bitmap. XBM contains monochrome, 1-bit bitmaps.
  u8g2.setFont(u8g2_font_ncenB14_tr);    //Set the font set, which is“u8g2_font_ncenB14_tr“
  u8g2.drawStr(32,30,"DFRobot");        //Start drawing strings at the coordinates of x-32, y-30 “DFRobot”
  u8g2.sendBuffer();        //Send the content of the memory frame buffer to the display.

  delay(2000);  

}
