/*!
 * @file Font.ino
 * @brief Display of several font directions supported in U8G2
 * @n U8G2 supports multiple fonts, and this demo shows only four directions (no mirrors are shown).
 * @n U8G2 Font GitHub Link：https://github.com/olikraus/u8g2/wiki/fntlistall
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


//width:30,height:30 
const uint8_t col[] U8X8_PROGMEM= {0x00,0xc0,0x00,0x00,0x00,0xe0,0x01,0x00,0x00,0xe0,0x01,0x00,0x00,
                                         0xc0,0x00,0x00,0x00,0xc0,0x00,0x00,0x00,0xc0,0x00,0x00,0x00,0xe0,
                                         0x01,0x00,0x00,0xf8,0x07,0x00,0x06,0xfe,0x1f,0x18,0x07,0xff,0x3f,
                                         0x38,0xdf,0xff,0xff,0x3e,0xfa,0xff,0xff,0x17,0xf0,0xff,0xff,0x03,
                                         0xe0,0xff,0xff,0x01,0xe0,0xff,0xff,0x01,0xe0,0xff,0xff,0x01,0xe0,
                                         0xff,0xff,0x01,0x20,0x00,0x00,0x01,0xa0,0xff,0x7f,0x01,0xa0,0x01,
                                         0x60,0x01,0x20,0x07,0x38,0x01,0xe0,0x0c,0xcc,0x01,0xe0,0x39,0xe7,
                                         0x01,0xe0,0xe7,0xf9,0x01,0xc0,0x1f,0xfe,0x00,0x80,0xff,0x7f,0x00,
                                         0x00,0xfe,0x1f,0x00,0x00,0xf8,0x07,0x00,0x00,0xe0,0x01,0x00,0x00,
                                         0xc0,0x00,0x00}; 

void setup() {
  u8g2.begin();
  u8g2.setFontPosTop();/**When you use drawStr to display strings, the default criteria is to display the lower-left
  * coordinates of the characters.The function can be understood as changing the coordinate position to the upper left
  * corner of the display string as the coordinate standard.*/
}

void Rotation() {
  u8g2.setFont(u8g2_font_bracketedbabies_tr);
  u8g2.firstPage(); 
  do {
    u8g2.drawXBMP( /* x=*/0 , /* y=*/0 , /* width=*/30 , /* height=*/30 , col );//Draw a XBM Bitmap. Position (x,y) is the upper left corner of the bitmap. XBM contains monochrome, 1-bit bitmaps.
	 /*@brief Set the drawing direction of all strings or glyphs setFontDirection(uint8_t dir)
     *@param dir=0
             dir=1, rotate 0°
             dir=2, rotate 180°
             dir=3, rotate 270°
    */
    u8g2.setFontDirection(0);			
    u8g2.drawStr( /* x=*/64,/* y=*/32, " DFR");		//Start drawing strings at the coordinates of x-64, y-32 “DFR”
    u8g2.setFontDirection(1);
    u8g2.drawStr(64,32, " DFR");
    u8g2.setFontDirection(2);
    u8g2.drawStr(64,32, " DFR");
    u8g2.setFontDirection(3);
    u8g2.drawStr(64,32, " DFR");
  }while(u8g2.nextPage()); 
  delay(1500);
}

void loop(void)
{ 
    u8g2.setFont( u8g2_font_sticker_mel_tr);
    for(int i = 0 ; i < 4 ; i++ )
    {
       switch(i)
       {
          case 0:
            u8g2.setFontDirection(0);
            break;
          case 1:
            u8g2.setFontDirection(1);
            break;
          case 2:
            u8g2.setFontDirection(2);
            break;
          case 3: 
            u8g2.setFontDirection(3);
            break;
       }
	   /*
       * firstPage will change the current page number position to 0
       * When modifications are between firstpage and nextPage, they will be re-rendered at each time.
       * This method consumes less ram space than sendBuffer
      */ 
      u8g2.firstPage();  
      do 
      {
        u8g2.drawStr(64, 32, "DFR");
        u8g2.drawXBMP( 0 , 0 , 30 , 30 , col );
      }while(u8g2.nextPage()); 
      delay(500);
    }
    Rotation();
}