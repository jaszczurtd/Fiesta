/*

  MessageBox.ino
  
  Example for the Message Box for U8x8

  Universal 8bit Graphics Library (https://github.com/olikraus/u8g2/)

  Copyright (c) 2016, olikraus@gmail.com
  All rights reserved.

  Redistribution and use in source and binary forms, with or without modification, 
  are permitted provided that the following conditions are met:

  * Redistributions of source code must retain the above copyright notice, this list 
    of conditions and the following disclaimer.
    
  * Redistributions in binary form must reproduce the above copyright notice, this 
    list of conditions and the following disclaimer in the documentation and/or other 
    materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND 
  CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, 
  INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR 
  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT 
  NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER 
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, 
  STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF 
  ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.  

*/

/*
	This demo can be displayed normally on OLED with a resolution of 128x64, 
	while it will be displayed incomplete in the 128x32 OLED.	
*/

#include <Arduino.h>
#include <U8x8lib.h>

#include <SPI.h>
#include <Wire.h>

// Please UNCOMMENT one of the contructor lines below
// U8x8 Contructor List 
// The complete list is available here: https://github.com/olikraus/u8g2/wiki/u8x8setupcpp
// Please update the pin numbers according to your setup. Use U8X8_PIN_NONE if the reset pin is not connected


/*
	Please use the corresponding instantiation-function when using the display 
	with different resolutions and communication ways.
*/
//  M0/ESP32/ESP8266/mega2560/Uno/Leonardo
//U8X8_SSD1306_128X64_NONAME_4W_HW_SPI u8x8(/* cs=*/ 10, /* dc=*/ 9, /* reset=*/ 8);
//U8X8_SSD1306_128X64_NONAME_HW_I2C u8x8(/* reset=*/ U8X8_PIN_NONE); 	      
//U8X8_SSD1306_128X32_UNIVISION_HW_I2C u8x8(/* reset=*/ U8X8_PIN_NONE);  

// End of constructor list

void setup(void)
{
  // DOGS102 Shield (http://shieldlist.org/controlconnection/dogs102)
  // u8x8.begin(/* menu_select_pin= */ 5, /* menu_next_pin= */ 4, /* menu_prev_pin= */ 2, /* menu_up_pin= */ U8X8_PIN_NONE, /* menu_down_pin= */ U8X8_PIN_NONE, /* menu_home_pin= */ 3);
  
  // DOGM128 Shield (http://shieldlist.org/schmelle2/dogm128) + DOGXL160 Shield
  //u8x8.begin(/* menu_select_pin= */ 2, /* menu_next_pin= */ 3, /* menu_prev_pin= */ 7, /* menu_up_pin= */ U8X8_PIN_NONE, /* menu_down_pin= */ U8X8_PIN_NONE, /* menu_home_pin= */ 8);
  
  // Arduboy
  //u8x8.begin(/*Select=*/ A0, /*Right/Next=*/ 5, /*Left/Prev=*/ 9, /*Up=*/ 8, /*Down=*/ 10, /*Home/Cancel=*/ A1); // Arduboy DevKit
  u8x8.begin(/*Select=*/ 7, /*Right/Next=*/ A1, /*Left/Prev=*/ A2, /*Up=*/ A0, /*Down=*/ A3, /*Home/Cancel=*/ 8); // Arduboy 10 (Production)
}

void loop(void)
{
  uint8_t r;
  u8x8.setFont(u8x8_font_chroma48medium8_r);
  r = u8x8_UserInterfaceMessage(u8x8.getU8x8(), "Message", "Box", NULL, " Ok \n Cancel ");
  if ( r == 0 )
  {
    u8x8.userInterfaceMessage("You pressed the", "Home/Quit", "Button", " Ok ");  
  }
  else if ( r == 1 )
  {
    u8x8.userInterfaceMessage("You selected the", "Ok", "Button", " Ok ");  
  }
  else if ( r == 2 )
  {
    u8x8.userInterfaceMessage("You selected the", "Cancel", "Button", " Ok ");  
  }
}

