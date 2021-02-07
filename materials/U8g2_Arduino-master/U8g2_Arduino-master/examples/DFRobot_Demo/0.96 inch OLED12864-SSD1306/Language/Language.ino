/*!
 * @file Font.ino
 * @brief Display multiple languages in U8G2
 * @n A demo for displaying “hello world！” in Chinese, English, Korean, Japanese
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



void setup(void) {
  u8g2.begin();    //init
  u8g2.enableUTF8Print();		// Enable UTF8 support for Arduino print（）function.
}

void loop(void) 
{
  /*
   *The font takes up a lot of memory, so please use it with caution. Get your own Chinese encode for displaying only several fixed words.
   *Display by drawXBM or use controller with larger memory
   *Chinese Font：require a controller with larger memory than Leonardo  
   *Japanese Font：require a controller with larger memory than UNO
   *Korean Font：Arduino INO files of the current version do not support for displaying Korean, but it can displayed properly on the Screen
  */
  u8g2.setFont(u8g2_font_unifont_t_chinese2);  //Set all fonts in “你好世界” to chinese2
  //u8g2.setFont(u8g2_font_b10_t_japanese1);  // Japanese 1 includes all fonts in “こんにちは世界” ：Lerning level 1-6  
  //u8g2.setFont(u8g2_font_unifont_t_korean1);  // Korean 1 includes all fonts in “안녕하세요세계”：Lerning level 1-2
  
  /*@brief Set font direction  of all strings setFontDirection(uint8_t dir)
   *@param dir=0，rotate 0 degree
                 dir=1，rotate 90 degrees
                 dir=2，rotate 180 degrees
                 dir=3，rotate 270 degrees
   *@param When completed font setting, re-set the cursor position to display normally. Refer to API description for more details.
  */
  u8g2.setFontDirection(0); 
  /*
   * firstPage Change the current page number position to 0 
   * Revise content in firstPage and nextPage, re-render everything every time
   * This method consumes less ram space than sendBuffer
  */
  u8g2.firstPage();
  do {
    u8g2.setCursor(/* x=*/0, /* y=*/15);    //Define the cursor of print function, any output of the print function will start at this position.
    u8g2.print("Hello World!");
    u8g2.setCursor(0, 30);
    u8g2.print("你好世界");		// Chinese "Hello World" 
    //u8g2.print("こんにちは世界");		// Japanese "Hello World" 
    //u8g2.print("안녕하세요 세계");   // Korean "Hello World" 
  } while ( u8g2.nextPage() );
  delay(1000);
}