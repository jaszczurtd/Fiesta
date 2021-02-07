/*!
 * @file Cube.ino
 * @brief Rotating 3D stereoscopic graphics
 * @n This is a simple rotating tetrahexon
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

#include <SPI.h>
//#include <Wire.h>


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
//U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(/* rotation=*/U8G2_R0, /* reset=*/ U8X8_PIN_NONE);    //  M0/ESP32/ESP8266/mega2560/Uno/Leonardo
U8G2_SSD1306_128X64_NONAME_1_4W_HW_SPI u8g2(/* rotation=*/U8G2_R0, /* cs=*/ 10, /* dc=*/ 9);


//2D array: The coordinates of all vertices of the tetrahesome are stored
double tetrahedron[4][3] = {{0,30,-30},{-30,-30,-30},{30,-30,-30},{0,0,30}};
void setup(void) {
  u8g2.begin();  
}

void loop(void) {
  /*
       * firstPage will change the current page number position to 0
       * When modifications are between firstpage and nextPage, they will be re-rendered at each time.
       * This method consumes less ram space than sendBuffer
   */ 
  u8g2.firstPage();
  do {
  //Connect the corresponding points inside the tetrahethal together
  u8g2.drawLine(OxyzToOu(tetrahedron[0][0], tetrahedron[0][2]), OxyzToOv(tetrahedron[0][1], tetrahedron[0][2]), OxyzToOu(tetrahedron[1][0], tetrahedron[1][2]), OxyzToOv(tetrahedron[1][1], tetrahedron[1][2]));
  u8g2.drawLine(OxyzToOu(tetrahedron[1][0], tetrahedron[1][2]), OxyzToOv(tetrahedron[1][1], tetrahedron[1][2]), OxyzToOu(tetrahedron[2][0], tetrahedron[2][2]), OxyzToOv(tetrahedron[2][1], tetrahedron[2][2]));
  u8g2.drawLine(OxyzToOu(tetrahedron[0][0], tetrahedron[0][2]), OxyzToOv(tetrahedron[0][1], tetrahedron[0][2]), OxyzToOu(tetrahedron[2][0], tetrahedron[2][2]), OxyzToOv(tetrahedron[2][1], tetrahedron[2][2]));
  u8g2.drawLine(OxyzToOu(tetrahedron[0][0], tetrahedron[0][2]), OxyzToOv(tetrahedron[0][1], tetrahedron[0][2]), OxyzToOu(tetrahedron[3][0], tetrahedron[3][2]), OxyzToOv(tetrahedron[3][1], tetrahedron[3][2]));
  u8g2.drawLine(OxyzToOu(tetrahedron[1][0], tetrahedron[1][2]), OxyzToOv(tetrahedron[1][1], tetrahedron[1][2]), OxyzToOu(tetrahedron[3][0], tetrahedron[3][2]), OxyzToOv(tetrahedron[3][1], tetrahedron[3][2]));
  u8g2.drawLine(OxyzToOu(tetrahedron[2][0], tetrahedron[2][2]), OxyzToOv(tetrahedron[2][1], tetrahedron[2][2]), OxyzToOu(tetrahedron[3][0], tetrahedron[3][2]), OxyzToOv(tetrahedron[3][1], tetrahedron[3][2]));
  // Rotate 0.1°
  rotate(0.1);
  
  } while ( u8g2.nextPage() );
  //delay(50);
}
/*！
 * @brief Convert xz in the three-dimensional coordinate system Oxyz
 * into the u coordinate inside the two-dimensional coordinate system Ouv
 * @param x in Oxyz  
 * @param z in Oxyz
 * @return u in Ouv
 */
int OxyzToOu(double x,double z){
   
   return (int)((x + 64) - z*0.35);
}


/*！
 * @brief Convert the yz in the three-dimensional coordinate system Oxyz into the v coordinate inside 
 * the two-dimensional coordinate system Ouv
 * @param y in Oxyz  
 * @param z in Oxyz
 * @return v in Ouv
 */ 
int OxyzToOv(double y,double z){
    return (int)((y + 35) - z*0.35);
}


/*!
 * @brief  Rotate the coordinates of all points of the entire 3D graphic around the Z axis
 * @param  angle represents the angle to rotate
 *     
 *  z rotation (z unchanged)
    x3 = x2 * cosb - y1 * sinb
    y3 = y1 * cosb + x2 * sinb
    z3 = z2
 */
void rotate(double angle)
{
  double rad, cosa, sina, Xn, Yn;
 
  rad = angle * PI / 180;
  cosa = cos(rad);
  sina = sin(rad);
  for (int i = 0; i < 4; i++)
  {
    Xn = (tetrahedron[i][0] * cosa) - (tetrahedron[i][1] * sina);
    Yn = (tetrahedron[i][0] * sina) + (tetrahedron[i][1] * cosa);

    //Store converted coordinates into an array of coordinates
    //Because it rotates around the Z-axis, the coordinates of the point z-axis remain unchanged
    tetrahedron[i][0] = Xn;
    tetrahedron[i][1] = Yn;
  }
}