//
//  utils.c
//  Index
//
//  Created by Marcin Kielesiński on 07/12/2019.
//

#include "utils.h"

int binatoi(char *s) {
    int i, l = 0, w = 1;
    
    for(i = 0; i < strlen(s); i++) {
        if (s [i] == '1')  {
            l += w;
            w *= 2;
        }
        if(s [i]=='0') {
            w *= 2;
        }
    }
    return(l);
}

static char binaryNum[16 + 1];
char *decToBinary(int n) {
    // array to store binary number
    int a = 0, c, k;
    
    memset(binaryNum, 0, sizeof(binaryNum));
    
    for (c = 15; c >= 0; c--) {
        k = n >> c;
        
        if (k & 1) {
            binaryNum[a++] = '1';
        } else {
            binaryNum[a++] = '0';
        }
    }
    return binaryNum;
}

unsigned char BinToBCD(unsigned char bin) {
    return ((((bin) / 10) << 4) + ((bin) % 10));
}


unsigned char reverse(unsigned char b) {
   b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
   b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
   b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
   return b;
}

void floatToDec(float val, int *hi, int *lo) {
	int t1 = (int)val;
	if(t1 > -128) {
		if(hi != NULL) {
			*hi = t1;
		}
		int t2 = (int) (((float)val - t1) * 10);
		if(lo != NULL) {
			if(t2 >= 0) {
				*lo = t2;
			} else {
				*lo = 0;
			}
		}
	}
}

float adcToVolt(float basev, int adc) {
    return adc * (basev/1024.0);    
}

static int samples[NUMSAMPLES];
float ntcToTemp(int tpin, int thermistor, int r) {

    uint8_t i;
    float average;

    // take N samples in a row, with a slight delay
    for (i=0; i< NUMSAMPLES; i++) {
        samples[i] = analogRead(tpin);
        delay(5);
    }

    // average all the samples out
    average = 0;
    for (i=0; i< NUMSAMPLES; i++) {
        average += samples[i];
    }
    average /= NUMSAMPLES;

    // convert the value to resistance
    average = 1023 / average - 1;
    average = r / average;

    float steinhart;
    steinhart = average / thermistor;     // (R/Ro)
    steinhart = log(steinhart);                  // ln(R/Ro)
    steinhart /= BCOEFFICIENT;                   // 1/B * ln(R/Ro)
    steinhart += 1.0 / (TEMPERATURENOMINAL + 273.15); // + (1/To)
    steinhart = 1.0 / steinhart;                 // Invert
    steinhart -= 273.15;                         // convert absolute temp to C    

    return steinhart;
}

void valToPWM(unsigned char pin, unsigned char val) {
    analogWrite(pin, (unsigned char)(255 - val));
}

void drawImage(int x, int y, int width, int height, int background, unsigned int *pointer) {
    Adafruit_ST7735 tft = returnReference();

    tft.fillRect(x, y, width, height, background);

    for(register int row = 0; row < height; row++) {
        for(register int col = 0; col < width; col++) {
            int px = pgm_read_word(pointer++);
            if(px != background) {
                tft.drawPixel(col + x, row + y, px);
            }
        }      
    }
}

int percentToWidth(float percent, int maxWidth) {
    return ((percent / 100) * (maxWidth - 2));
}

int textWidth(const char* text) {
    Adafruit_ST7735 tft = returnReference();
    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    return w;
}

int textHeight(const char* text) {
    Adafruit_ST7735 tft = returnReference();
    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    return h;
}

void drawTempValue(int x, int y, int valToDisplay) {
    Adafruit_ST7735 tft = returnReference();

    tft.setFont();
    tft.setTextSize(1);
    tft.setTextColor(ST7735_BLACK);
    tft.setCursor(x, y);

    tft.fillRect(x, y, 22, 8, BIG_ICONS_BG_COLOR);

    char temp[8];
    memset(temp, 0, sizeof(temp));
    snprintf(temp, sizeof(temp) - 1, "%d", valToDisplay);

    tft.println(temp);
}

int currentValToHeight(int currentVal, int maxVal) {
    float percent = (currentVal * 100) / maxVal;
    return percentToWidth(percent, TEMP_BAR_MAXHEIGHT);
}

void drawTempBar(int x, int y, int currentHeight, int color) {
    Adafruit_ST7735 tft = returnReference();
    
    tft.fillRect(x, y, 3, currentHeight, color);
    tft.fillRect(x, y - 2, 3, 2, BIG_ICONS_BG_COLOR);
}

void displayErrorWithMessage(int x, int y, const char *msg) {
    Adafruit_ST7735 tft = returnReference();

    int workingx = x; 
    int workingy = y;

    tft.fillCircle(workingx, workingy, 10, ST77XX_RED);
    workingx += 20;
    tft.fillCircle(workingx, workingy, 10, ST77XX_RED);

    workingy += 5;
    workingx = x + 4;

    tft.fillRoundRect(workingx, workingy, 15, 40, 6, ST77XX_RED);
    workingy +=30;
    tft.drawLine(workingx, workingy, workingx + 14, workingy, ST77XX_BLACK);

    workingx +=6;
    workingy +=4;
    tft.drawLine(workingx, workingy, workingx, workingy + 5, ST77XX_BLACK);
    tft.drawLine(workingx + 1, workingy, workingx + 1, workingy + 5, ST77XX_BLACK);

    workingx = x -16;
    workingy = y;
    tft.drawLine(workingx, workingy, workingx + 15, workingy, ST77XX_BLACK);

    workingy = y - 8;
    tft.drawLine(workingx, workingy, workingx + 15, y - 2, ST77XX_BLACK);

    workingy = y + 8;
    tft.drawLine(workingx, workingy, workingx + 15, y + 2, ST77XX_BLACK);

    workingx = x + 23;
    workingy = y;
    tft.drawLine(workingx, workingy, workingx + 15, workingy, ST77XX_BLACK);
  
    workingy = y - 3;
    tft.drawLine(workingx, workingy + 1, workingx + 15, y - 8, ST77XX_BLACK);

    workingy = y + 1;
    tft.drawLine(workingx, workingy + 1, workingx + 15, y + 8, ST77XX_BLACK);

    tft.setCursor(x + 8, y + 46);
    tft.setTextColor(ST77XX_BLUE);
    tft.setTextSize(1);
    tft.println(msg);
}

PCF8574 expander = PCF8574(PCF8574_ADDR);
void pcf8574(unsigned char pin, bool value) {
    expander.write(pin, value);
}

void i2cScanner(void) {
  byte error, address;
  int nDevices;
 
  Serial.println("Scanning...");
 
  nDevices = 0;
  for(address = 1; address < 127; address++ ) {
    // The i2c_scanner uses the return value of
    // the Write.endTransmisstion to see if
    // a device did acknowledge to the address.
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
 
    if (error == 0) {
      Serial.print("I2C device found at address 0x");
      if (address<16)
        Serial.print("0");
      Serial.print(address,HEX);
      Serial.println("  !");
 
      nDevices++;
    }
    else if (error==4) {
      Serial.print("Unknown error at address 0x");
      if (address<16)
        Serial.print("0");
      Serial.println(address,HEX);
    }    
  }
  if (nDevices == 0)
    Serial.println("No I2C devices found\n");
  else
    Serial.println("done\n");
 
  delay(5000);           // wait 5 seconds for next scan
}

void init4051(void) {
    pinMode(13, OUTPUT);  //C
    pinMode(12, OUTPUT);  //B  
    pinMode(11, OUTPUT);  //A

    set4051ActivePin(0);
}

void set4051ActivePin(unsigned char pin) {
    digitalWrite(11, (pin & 0x01) > 0); 
    digitalWrite(12, (pin & 0x02) > 0); 
    digitalWrite(13, (pin & 0x04) > 0); 
}
