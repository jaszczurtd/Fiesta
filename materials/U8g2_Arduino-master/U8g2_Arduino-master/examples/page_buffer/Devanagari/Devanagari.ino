/*

  Devanagari.ino

  Universal 8bit Graphics Library (https://github.com/olikraus/u8g2/)

  Copyright (c) 2019, olikraus@gmail.com
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
  
  This example shows how to print Devanagari glyphs.
  A new function "u8g2_draw_unifont_devanagari" is introduced here,
  which will modify the glyph position for proper display of a word.
  
  This function is incomplete and will not work for all available gylphs.
  Please extend the function by yourself. I would be happy to hear about
  new, improved versions of "u8g2_draw_unifont_devanagari".
  
   
*/

/*
	This demo can be displayed normally on OLED with a resolution of 128x64, 
	while it will be displayed incomplete in the 128x32 OLED.	
*/

#include <Arduino.h>
#include <U8g2lib.h>

#include <SPI.h>
#include <Wire.h>


/*
  U8g2lib Example Overview:
    Frame Buffer Examples: clearBuffer/sendBuffer. Fast, but may not work with all Arduino boards because of RAM consumption
    Page Buffer Examples: firstPage/nextPage. Less RAM usage, should work with all Arduino boards.
    U8x8 Text Only Example: No RAM usage, direct communication with display controller. No graphics, 8x8 Text only.
    
  This is a page buffer example.    
*/

// Please UNCOMMENT one of the contructor lines below
// U8g2 Contructor List (Picture Loop Page Buffer)
// The complete list is available here: https://github.com/olikraus/u8g2/wiki/u8g2setupcpp
// Please update the pin numbers according to your setup. Use U8X8_PIN_NONE if the reset pin is not connected


/*
	Please use the corresponding instantiation-function when using the display 
	with different resolutions and communication ways.
*/
//  M0/ESP32/ESP8266/mega2560/Uno/Leonardo
//U8G2_SSD1306_128X64_NONAME_1_4W_HW_SPI u8g2(U8G2_R0, /* cs=*/ 10, /* dc=*/ 9);
//U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
//U8G2_SSD1306_128X32_UNIVISION_1_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);   



// End of constructor list


/*
  draw unicode for https://en.wikipedia.org/wiki/Devanagari
  Adjust the glyph position as good as possible for the unicode font.

  Report missing or wrong glyph adjustments here:
  https://github.com/olikraus/u8g2/issues/584
  
  precondition: 
    u8g2_SetFont(&u8g2, u8g2_font_unifont_t_devanagari);
    u8g2_SetFontMode(&u8g2, 1);
    Font direction command is NOT supported
*/
u8g2_uint_t u8g2_draw_unifont_devanagari(u8g2_uint_t x, u8g2_uint_t y, const char *str)
{
  uint16_t e;
  u8g2_uint_t delta, sum;
  u8g2.getU8g2()->u8x8.next_cb = u8x8_utf8_next;
  u8x8_utf8_init(u8g2.getU8x8());
  sum = 0;
  for(;;)
  {
    e = u8g2.getU8g2()->u8x8.next_cb(u8g2.getU8x8(), (uint8_t)*str);
    if ( e == 0x0ffff )
      break;
    str++;
    if ( e != 0x0fffe )
    {
      
      switch(e)
      {
	/* many more glyphs and corrections are missing */
	/* please report to https://github.com/olikraus/u8g2/issues/584 */
	case 0x093e: x-= 12; break;
	case 0x093f: x-= 19; break;
	case 0x0941: x-= 10; y+=3; break;		// move down
	case 0x0947: x-= 12; break;
	case 0x094d: x-= 10; break;
      }
      delta = u8g2.drawGlyph(x, y, e);    
      switch(e)
      {
	case 0x0941: x-= 3; y -=3; break;		// revert the y shift
	case 0x094d: x-= 8; break;
      }
      x += delta;
      sum += delta;    
    }
  }
  return sum;
}



void setup(void) {

  /* U8g2 Project: SSD1306 Test Board */
  //pinMode(10, OUTPUT);
  //pinMode(9, OUTPUT);
  //digitalWrite(10, 0);
  //digitalWrite(9, 0);		

  /* U8g2 Project: T6963 Test Board */
  //pinMode(18, OUTPUT);
  //digitalWrite(18, 1);	

  /* U8g2 Project: KS0108 Test Board */
  //pinMode(16, OUTPUT);
  //digitalWrite(16, 0);	

  /* U8g2 Project: LC7981 Test Board, connect RW to GND */
  //pinMode(17, OUTPUT);
  //digitalWrite(17, 0);	

  /* U8g2 Project: Pax Instruments Shield: Enable Backlight */
  //pinMode(6, OUTPUT);
  //digitalWrite(6, 0);	

  u8g2.begin();  
}

void loop(void) {

  /* Set the unifont with Devanagari glyphs */
  u8g2.setFont(u8g2_font_unifont_t_devanagari);
  
  /* Important: do not write background pixel */
  u8g2.setFontMode(1);

  u8g2.firstPage();
  do {
    u8g2_draw_unifont_devanagari(0,24,"नमस्ते दुनिया");	// Hello World
  } while ( u8g2.nextPage() );
  //delay(1000);
}

