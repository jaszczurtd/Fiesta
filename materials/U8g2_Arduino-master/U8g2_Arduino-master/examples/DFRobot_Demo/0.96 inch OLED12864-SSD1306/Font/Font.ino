/*!
 * @file Font.ino
 * @brief Show several U8G2-supported fonts
 * @n U8G2 supports multiple fonts, and this demo is just a few fonts to display
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

void setup(void)
{
  u8g2.begin();       //Initialize the function
  u8g2.setFontPosTop();  /**When you use drawStr to display strings, the default criteria is to display the lower-left
  * coordinates of the characters.The function can be understood as changing the coordinate position to the upper left
  * corner of the display string as the coordinate standard.*/
}


void draw(int a )
{
   switch(a)
   {
     case 0: 
     u8g2.setFont(u8g2_font_bubble_tr   );    //Set the font set, which is“u8g2_font_bubble_tr“
     u8g2.drawStr(/* x=*/0,/* y=*/0, "DFR123");       //Start drawing strings at the coordinates of x-0, y-0 “DFR123”
     u8g2.setFont(u8g2_font_lucasarts_scumm_subtitle_o_tf     );
     u8g2.drawStr(0, 25, "DFR123");  
     u8g2.setFont(u8g2_font_HelvetiPixelOutline_tr     );
     u8g2.drawStr(0, 45, "DFR123"); 
     break;            
     case 1: 
     u8g2.setFont(u8g2_font_tenstamps_mr    );
     u8g2.drawStr(0,0, "DFR123");  
     u8g2.setFont(u8g2_font_jinxedwizards_tr      );
     u8g2.drawStr(56, 16, "DFR123");  
     u8g2.setFont(u8g2_font_secretaryhand_tr      );
     u8g2.drawStr(0, 32, "DFR123"); 
     u8g2.setFont(u8g2_font_freedoomr10_mu       );
     u8g2.drawStr(56, 48, "DFR123"); 
     break;                     
   }	   
}

 void loop()
{
  for( int i = 0; i <2 ; i++)
  {
  /*
       * firstPage will change the current page number position to 0
       * When modifications are between firstpage and nextPage, they will be re-rendered at each time.
       * This method consumes less ram space than sendBuffer
   */ 
    u8g2.firstPage();   
    do
	  {
		  draw(i);
	  } while( u8g2.nextPage() );
	  delay(2000);
  }
}