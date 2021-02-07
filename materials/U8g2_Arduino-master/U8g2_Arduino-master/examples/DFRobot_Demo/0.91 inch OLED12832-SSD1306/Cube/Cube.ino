/*!
 * @file Cube.ino
 * @brief Rotating 3D stereoscopic graphics
 * @n This is a simple rotating tetrahexon
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
U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);//  M0/ESP32/ESP8266/mega2560/Uno/Leonardo

//2D array: The coordinates of all vertices of the tetrahesome are stored
double tetrahedron[4][3] = {{0,15,-15},{-15,-15,-15},{15,-15,-15},{0,0,15}};
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
  rotate(1);
  
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
    return (int)((y + 15) - z*0.35);
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
