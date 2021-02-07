/*

  GraphicsTest.ino

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


//  M0/ESP32/ESP8266/mega2560/Uno/Leonardo
//U8G2_SSD1306_128X64_NONAME_1_4W_HW_SPI u8g2(U8G2_R0, /* cs=*/ 10, /* dc=*/ 9);
//U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
//U8G2_SSD1306_128X32_UNIVISION_1_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);  



// End of constructor list


void u8g2_prepare(void) {
  u8g2.setFont(u8g2_font_6x10_tf);	//Set the font to "u8g2_font_6x10_tf"
  u8g2.setFontRefHeightExtendedText();	//Ascent will be the largest ascent of "A", "1" or "(" of the current font. Descent will be the descent of "g" or "(" of the current font.
  u8g2.setDrawColor(1);	//Ascent will be the largest ascent of "A", "1" or "(" of the current font. Descent will be the descent of "g" or "(" of the current font.
  u8g2.setFontPosTop();	 /*When you use drawStr to display strings, the default criteria is to display the lower-left coordinates of the characters.
                        XXXX  */
  u8g2.setFontDirection(0);	//Set the screen orientation: 0 -- for normal display
}

void u8g2_box_title(uint8_t a) {
  u8g2.drawStr( 10+a*2, 5, "U8g2"); //Draw string "U8g2"
  u8g2.drawStr( 10, 20, "GraphicsTest");
  
  u8g2.drawFrame(0,0,u8g2.getDisplayWidth(),u8g2.getDisplayHeight() );	//Start drawing an empty box of width w and height h at a coordinate of (0,0)
}

/*
 * Draw solid squares and hollow squares
*/
void u8g2_box_frame(uint8_t a) {
  u8g2.drawStr( 0, 0, "drawBox");
  u8g2.drawBox(5,10,20,10);	//Start drawing a solid square with a width w and a height h at a position with coordinates of (5, 10)
  u8g2.drawBox(10+a,15,30,7);
  u8g2.drawStr( 0, 30, "drawFrame");
  u8g2.drawFrame(5,10+30,20,10);
  u8g2.drawFrame(10+a,15+30,30,7);
}

/*
 * Draw solid and hollow circles
*/
void u8g2_disc_circle(uint8_t a) {
  u8g2.drawStr( 0, 0, "drawDisc");
  u8g2.drawDisc(10,18,9);	//Draw a solid circle with a radius of 10 at the position (10, 18)
  u8g2.drawDisc(24+a,16,7);
  u8g2.drawStr( 0, 30, "drawCircle");
  u8g2.drawCircle(10,18+30,9);	//Draw a hollow circle with a radius of 10 at the position (10, 18+30)
  u8g2.drawCircle(24+a,16+30,7);
}

/*
 * Draw solid and hollow boxes
*/
void u8g2_r_frame(uint8_t a) {
  u8g2.drawStr( 0, 0, "drawRFrame/Box");
  u8g2.drawRFrame(5, 10,40,30, a+1);	//At the position (5 ,10) start drawing with a width of 40 and a height of 30 a frame with a radius of a+1 circular edge (hollow).
  u8g2.drawRBox(50, 10,25,40, a+1);		//At the position (50,10) start drawing with a width of 25 and a height of 40 a frame with a circular edge with a radius of a 1 (solid).
}

void u8g2_string(uint8_t a) {
/*@brief Set the drawing direction of all strings or glyphs setFontDirection(uint8_t dir)
     *@param dir=0
             dir=1, rotate 0°
             dir=2, rotate 180°
             dir=3, rotate 270°
    */
  u8g2.setFontDirection(0);	
  u8g2.drawStr(30+a,31, " 0");
  u8g2.setFontDirection(1);	
  u8g2.drawStr(30,31+a, " 90");
  u8g2.setFontDirection(2);	
  u8g2.drawStr(30-a,31, " 180");
  u8g2.setFontDirection(3);	
  u8g2.drawStr(30,31-a, " 270");
}

/*
 * Draw segments
*/
void u8g2_line(uint8_t a) {
  u8g2.drawStr( 0, 0, "drawLine");
  u8g2.drawLine(7+a, 10, 40, 55);	//Draw a line between two points. (Argument is two-point coordinates)
  u8g2.drawLine(7+a*2, 10, 60, 55);
  u8g2.drawLine(7+a*3, 10, 80, 55);
  u8g2.drawLine(7+a*4, 10, 100, 55);
}

/*
 * Draw solid triangles and hollow triangles
*/
void u8g2_triangle(uint8_t a) {
  uint16_t offset = a;
  u8g2.drawStr( 0, 0, "drawTriangle");
  u8g2.drawTriangle(14,7, 45,30, 10,40);	//Draw a triangle (solid polygon). (Argument is triangle three vertex coordinates)
  u8g2.drawTriangle(14+offset,7-offset, 45+offset,30-offset, 57+offset,10-offset);
  u8g2.drawTriangle(57+offset*2,10, 45+offset*2,30, 86+offset*2,53);
  u8g2.drawTriangle(10+offset,40+offset, 45+offset,30+offset, 86+offset,53+offset);
}

/*
 * Show characters in the ASCII code table
*/
void u8g2_ascii_1() {
  char s[2] = " ";
  uint8_t x, y;
  u8g2.drawStr( 0, 0, "ASCII page 1");
  for( y = 0; y < 6; y++ ) {
    for( x = 0; x < 16; x++ ) {
      s[0] = y*16 + x + 32;
      u8g2.drawStr(x*7, y*10+10, s);
    }
  }
}

void u8g2_ascii_2() {
  char s[2] = " ";
  uint8_t x, y;
  u8g2.drawStr( 0, 0, "ASCII page 2");
  for( y = 0; y < 6; y++ ) {
    for( x = 0; x < 16; x++ ) {
      s[0] = y*16 + x + 160;
      u8g2.drawStr(x*7, y*10+10, s);
    }
  }
}

/*
 * Draw a string icon in a UTF-8 encoding
*/
void u8g2_extra_page(uint8_t a)
{
  u8g2.drawStr( 0, 0, "Unicode");
  u8g2.setFont(u8g2_font_unifont_t_symbols);
  u8g2.setFontPosTop();
  u8g2.drawUTF8(0, 24, "☀ ☁");	//Start drawing a string icon encoded as UTF-8 at the location (0,24)
  switch(a) {
    case 0:
    case 1:
    case 2:
    case 3:
      u8g2.drawUTF8(a*3, 36, "☂");
      break;
    case 4:
    case 5:
    case 6:
    case 7:
      u8g2.drawUTF8(a*3, 36, "☔");
      break;
  }
}

/*
 * Show the reverse display of font. Which means the font is displayed interchangeable with 
 * the background color. (For example, it would have been black on white, replaced by white
 * on black)
*/
void u8g2_xor(uint8_t a) {
  uint8_t i;
  u8g2.drawStr( 0, 0, "XOR");
  u8g2.setFontMode(1);
  u8g2.setDrawColor(2);
  for( i = 0; i < 5; i++)
  {
    u8g2.drawBox(10+i*16, 18 + (i&1)*4, 21,31);
  }
  u8g2.drawStr( 5+a, 19, "XOR XOR XOR XOR");
  u8g2.setDrawColor(0);
  u8g2.drawStr( 5+a, 29, "CLR CLR CLR CLR");
  u8g2.setDrawColor(1);
  u8g2.drawStr( 5+a, 39, "SET SET SET SET");
  u8g2.setFontMode(0);
    
}

/*
 *This is the data of converting a graph to bitmap for using the drawXBMP () to display. 
*/
#define cross_width 24
#define cross_height 24
static const unsigned char cross_bits[] U8X8_PROGMEM  = {
  0x00, 0x18, 0x00, 0x00, 0x24, 0x00, 0x00, 0x24, 0x00, 0x00, 0x42, 0x00, 
  0x00, 0x42, 0x00, 0x00, 0x42, 0x00, 0x00, 0x81, 0x00, 0x00, 0x81, 0x00, 
  0xC0, 0x00, 0x03, 0x38, 0x3C, 0x1C, 0x06, 0x42, 0x60, 0x01, 0x42, 0x80, 
  0x01, 0x42, 0x80, 0x06, 0x42, 0x60, 0x38, 0x3C, 0x1C, 0xC0, 0x00, 0x03, 
  0x00, 0x81, 0x00, 0x00, 0x81, 0x00, 0x00, 0x42, 0x00, 0x00, 0x42, 0x00, 
  0x00, 0x42, 0x00, 0x00, 0x24, 0x00, 0x00, 0x24, 0x00, 0x00, 0x18, 0x00, };

#define cross_fill_width 24
#define cross_fill_height 24
static const unsigned char cross_fill_bits[] U8X8_PROGMEM  = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x18, 0x64, 0x00, 0x26, 
  0x84, 0x00, 0x21, 0x08, 0x81, 0x10, 0x08, 0x42, 0x10, 0x10, 0x3C, 0x08, 
  0x20, 0x00, 0x04, 0x40, 0x00, 0x02, 0x80, 0x00, 0x01, 0x80, 0x18, 0x01, 
  0x80, 0x18, 0x01, 0x80, 0x00, 0x01, 0x40, 0x00, 0x02, 0x20, 0x00, 0x04, 
  0x10, 0x3C, 0x08, 0x08, 0x42, 0x10, 0x08, 0x81, 0x10, 0x84, 0x00, 0x21, 
  0x64, 0x00, 0x26, 0x18, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, };

#define cross_block_width 14
#define cross_block_height 14
static const unsigned char cross_block_bits[] U8X8_PROGMEM  = {
  0xFF, 0x3F, 0x01, 0x20, 0x01, 0x20, 0x01, 0x20, 0x01, 0x20, 0x01, 0x20, 
  0xC1, 0x20, 0xC1, 0x20, 0x01, 0x20, 0x01, 0x20, 0x01, 0x20, 0x01, 0x20, 
  0x01, 0x20, 0xFF, 0x3F, };

/*
 * Draw a bitmap
*/
void u8g2_bitmap_overlay(uint8_t a) {
  uint8_t frame_size = 28;

  u8g2.drawStr(0, 0, "Bitmap overlay");

  u8g2.drawStr(0, frame_size + 12, "Solid / transparent");
  u8g2.setBitmapMode(false /* solid */);
  u8g2.drawFrame(0, 10, frame_size, frame_size);
  u8g2.drawXBMP(2, 12, cross_width, cross_height, cross_bits);
  if(a & 4)
    u8g2.drawXBMP(7, 17, cross_block_width, cross_block_height, cross_block_bits);
  /*
 * Set the pattern of the bitmap to define whether the background color is written to the bitmap function  
 * (Mode0/solid，is_transparent = 0).
 * Or not write the background color to the bitmap function. (Mode1/solid，is_transparent = 1). 
 * The default mode is 0(fixed mode).
*/
  u8g2.setBitmapMode(true /* transparent*/);
  u8g2.drawFrame(frame_size + 5, 10, frame_size, frame_size);
  /*
 * The position (x,y) is the upper-left corner of the bitmap. XBM contains a monochrome 1-bit bitmap.
 * The current color index is used for drawing (refers to setColorIndex) pixel value1.
*/
  u8g2.drawXBMP(frame_size + 7, 12, cross_width, cross_height, cross_bits);
  if(a & 4)
    u8g2.drawXBMP(frame_size + 12, 17, cross_block_width, cross_block_height, cross_block_bits);
}

void u8g2_bitmap_modes(uint8_t transparent) {
  const uint8_t frame_size = 24;

  u8g2.drawBox(0, frame_size * 0.5, frame_size * 5, frame_size);
  u8g2.drawStr(frame_size * 0.5, 50, "Black");
  u8g2.drawStr(frame_size * 2, 50, "White");
  u8g2.drawStr(frame_size * 3.5, 50, "XOR");
  
  if(!transparent) {
    u8g2.setBitmapMode(false /* solid */);
    u8g2.drawStr(0, 0, "Solid bitmap");
  } else {
    u8g2.setBitmapMode(true /* transparent*/);
    u8g2.drawStr(0, 0, "Transparent bitmap");
  }
  u8g2.setDrawColor(0);// Black
  u8g2.drawXBMP(frame_size * 0.5, 24, cross_width, cross_height, cross_bits);//绘制位图
  u8g2.setDrawColor(1); // White
  u8g2.drawXBMP(frame_size * 2, 24, cross_width, cross_height, cross_bits);
  u8g2.setDrawColor(2); // XOR
  u8g2.drawXBMP(frame_size * 3.5, 24, cross_width, cross_height, cross_bits);
}

//Define the initial variable for the drawing state
uint8_t draw_state = 0;

/*
 * Draw functions: call other functions in an orderly manner.
*/
void draw(void) {
  u8g2_prepare();
  switch(draw_state >> 3) {
    case 0: u8g2_box_title(draw_state&7); break;
    case 1: u8g2_box_frame(draw_state&7); break;
    case 2: u8g2_disc_circle(draw_state&7); break;
    case 3: u8g2_r_frame(draw_state&7); break;
    case 4: u8g2_string(draw_state&7); break;
    case 5: u8g2_line(draw_state&7); break;
    case 6: u8g2_triangle(draw_state&7); break;
    case 7: u8g2_ascii_1(); break;
    case 8: u8g2_ascii_2(); break;
    case 9: u8g2_extra_page(draw_state&7); break;
    case 10: u8g2_xor(draw_state&7); break;
    case 11: u8g2_bitmap_modes(0); break;
    case 12: u8g2_bitmap_modes(1); break;
    case 13: u8g2_bitmap_overlay(draw_state&7); break;
  }
}


void setup(void) {
  u8g2.begin(); //Initialize the function
}

void loop(void) {
  // picture loop  
  u8g2.firstPage();  
  do {
    draw();
  } while( u8g2.nextPage() );
  
  // increase the state
  draw_state++;
  if ( draw_state >= 14*8 )
    draw_state = 0;

  // delay between each page
  delay(150);

}