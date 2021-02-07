/*!
 * @file FontDirection.ino
 * @brief Display of several font directions supported in U8G2
 * @n U8G2 supports multiple fonts, and this demo shows only four directions (no mirrors are shown).
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

const uint8_t col[] U8X8_PROGMEM= {0x00,0xc0,0x00,0x00,0x00,0xe0,0x01,0x00,0x00,0xe0,0x01,0x00,0x00,0xc0,0x00,0x00,
                                         0x00,0xc0,0x00,0x00,0x00,0xc0,0x00,0x00,0x00,0xe0,0x01,0x00,0x00,0xf8,0x07,0x00,
                                         0x06,0xfe,0x1f,0x18,0x07,0xff,0x3f,0x38,0xdf,0xff,0xff,0x3e,0xfa,0xff,0xff,0x17,
                                         0xf0,0xff,0xff,0x03,0xe0,0xff,0xff,0x01,0xe0,0xff,0xff,0x01,0xe0,0xff,0xff,0x01,
                                         0xe0,0xff,0xff,0x01,0x20,0x00,0x00,0x01,0xa0,0xff,0x7f,0x01,0xa0,0x01,0x60,0x01,
                                         0x20,0x07,0x38,0x01,0xe0,0x0c,0xcc,0x01,0xe0,0x39,0xe7,0x01,0xe0,0xe7,0xf9,0x01,
                                         0xc0,0x1f,0xfe,0x00,0x80,0xff,0x7f,0x00,0x00,0xfe,0x1f,0x00,0x00,0xf8,0x07,0x00,
                                         0x00,0xe0,0x01,0x00,0x00,0xc0,0x00,0x00}; // 图片的数据，尺寸为30x30

                                         

void setup() {
  u8g2.begin();  //Initialize the function
}

void loop(void)
{ 
    u8g2.clearBuffer();    // Clears all pixel in the memory frame buffer. 

    u8g2.drawXBMP( /* x=*/0 , /* y=*/0 , /* width=*/30 , /* height=*/30 , col );     //Draw a XBM Bitmap. Position (x,y) is the upper left corner of the bitmap. XBM contains monochrome, 1-bit bitmaps.
    
    u8g2.setFont(u8g2_font_pxplusibmvga9_tf);   //Set the font set, which is“u8g2_font_pxplusibmvga9_tf“
    for(int i = 0; i < 4;i++)
    {
      switch(i)
      {
        case 0:
        /*@brief Set the drawing direction of all strings or glyphs setFontDirection(uint8_t dir)
     *@param dir=0
             dir=1, rotate 0°
             dir=2, rotate 180°
             dir=3, rotate 270°
    */
        u8g2.setFontDirection(0);           
        u8g2.drawStr(/* x=*/33, /* y=*/32, "DFR");
        break;
        
        case 1:
        u8g2.setFontDirection(1);
        u8g2.drawStr(66, 7, "DFR");
        break;
        
        case 2:
        u8g2.setFontDirection(2);
        u8g2.drawStr(110, 21, "DFR");
        break;
        
        case 3:
        u8g2.setFontDirection(3);
        u8g2.drawStr(128, 32, "DFR");
        break;
      }
      delay(500);
      u8g2.sendBuffer();//Send the content of the memory frame buffer to the display.
    }

}
