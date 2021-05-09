//
//  utils.c
//  Index
//
//  Created by Marcin Kielesiński on 07/12/2019.
//

#include "utils.h"

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
    return adc * (basev / 1024.0);    
}

float getAverageValueFrom(int tpin) {

    uint8_t i;
    float average = 0;

    // take N samples in a row, with a slight delay
    for (i = 0; i < NUMSAMPLES; i++) {
        average += analogRead(tpin);
        delay(5);
    }
    average /= NUMSAMPLES;

    return average;
}

float ntcToTemp(int tpin, int thermistor, int r) {

    float average = getAverageValueFrom(tpin);

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

int percentToWidth(float percent, int maxWidth) {
    return ((percent / 100) * (maxWidth - 2));
}

int currentValToHeight(int currentVal, int maxVal) {
    float percent = (currentVal * 100) / maxVal;
    return percentToWidth(percent, TEMP_BAR_MAXHEIGHT);
}

PCF8574 expander = PCF8574(PCF8574_ADDR);
void pcf8574(unsigned char pin, bool value) {
    expander.write(pin, value);
}

#ifdef I2C_SCANNER
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
      if (address < 16)
        Serial.print("0");
      Serial.print(address, HEX);
      Serial.println("  !");
 
      nDevices++;
    }
    else if (error==4) {
      Serial.print("Unknown error at address 0x");
      if (address < 16)
        Serial.print("0");
      Serial.println(address, HEX);
    }    
  }
  if (nDevices == 0)
    Serial.println("No I2C devices found\n");
  else
    Serial.println("done\n");
 
  delay(5000);           // wait 5 seconds for next scan
}
#endif

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
