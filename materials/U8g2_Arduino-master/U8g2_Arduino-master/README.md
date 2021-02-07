# U8g2_Arduino: Arduino Monochrome Graphics Library

![https://raw.githubusercontent.com/wiki/olikraus/u8g2/img/uc1701_dogs102_uno_board_320.jpg](https://raw.githubusercontent.com/wiki/olikraus/u8g2/img/uc1701_dogs102_uno_board_320.jpg) 

U8glib V2 library for Arduino

Description: https://github.com/olikraus/u8g2/wiki

Issue Tracker: https://github.com/olikraus/u8g2/issues

Download (2.26.14): https://github.com/olikraus/U8g2_Arduino/archive/master.zip

## U8g2_Arduino:Monochrome image display library
* 
`U8g2_Arduino` is a powerful monochrome image display library that provides universal basic display (points, lines, circles, etc.), various commonly used icons, and beautiful fonts.
DFRobot has modified `U8g2_Arduino` based on its own products:
    1. Added the DFRobot_Demo folder to provide product-specific Demos.
    1. All materialized functions of Demo have been deleted.


## Function list
`U8g2_Arduino` integrates a large number of functional components. Can achieve rich monochrome display effects.

`U8g2_Arduino` API functions:https://github.com/olikraus/u8g2/wiki/u8g2reference#updatedisplayarea=%E5%BA%93%E6%96%87%E4%BB%B6

`U8g2_Arduino` Font parameter list:https://github.com/olikraus/u8g2/wiki/fntlistall

## U8g2_Arduino compatibility
`U8g2_Arduino` can support a variety of MCUs. The following table shows the supported MCU models.

MCU          |     Uno      |   Leonardo    |     FireBeetleESP32     |    FireBeetleESP8266    |     m0         |     BK7251      
-----------  | :----------: |  :----------: |  :----------: |  :----------: | :------------: |   ------------
U8g2_Arduino |      √       |       √       |      √        |       √       |       √        |  


## `U8g2_Arduino` supported screens
 
`U8g2_Arduino` currently supports a variety of display driver ICs and resolutions. The following table shows the currently available drivers and screens.When the product is updated, we will update the `U8g2_Arduino` simultaneously.<br>

* SSD1306 0.91" OLED-A<br>
[产品图片]
* SSD1306 0.91" OLED-B<br>
[产品图片]
* SSD1306 0.96" OLED<br>
[产品图片]


## Install `U8g2_Arduino` firmware
`U8g2_Arduino` can be used by people with different software development levels.For beginners, you only need to have the basics of Arduino to perform a variety of displays.`U8g2_Arduino` API function link can help you learn.

1. DownloadArduino IDE<br>
1. Download the `U8g2_Arduino` code from github<br>
1. Open "GraphicsTest.ino" from the downloaded file by Arduino IDE<br>
1. Connect your Arduino Uno board<br>
1. Select Tools> Board: Arduino Uno and Tools> Port: Select your Arduino board<br>
1. Click "Verify" and "Upload" the software to your development board



## Common problem

1、Q: Why can't some Demo run on Arduino UNO / Leonardo?
    
 * Answer: Because of the memory size of Arduino UNO / Leonardo, the entire program cannot be stored;You can change to a master with a larger memory, such as Firebettle-ESP32.We have corresponding instructions in each ino header file, you can read it in detail.<br>

2、Q: Why does Language.ino in Korean not display properly in Arduino IDE?

*  A: This is due to Arduino coding.Although it does not display properly in the sample program, the screen displays normally after uploading the code.
